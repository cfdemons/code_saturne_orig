# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------------

# This file is part of Code_Saturne, a general-purpose CFD tool.
#
# Copyright (C) 1998-2021 EDF S.A.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301, USA.

#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
# Standard modules import
#-------------------------------------------------------------------------------

import os, sys
import shutil, re
import subprocess
import threading
import string
import time
import logging
import fnmatch

#-------------------------------------------------------------------------------
# Application modules import
#-------------------------------------------------------------------------------

from code_saturne.cs_exec_environment import get_shell_type, enquote_arg
from code_saturne.cs_compile import files_to_compile, compile_and_link
from code_saturne import cs_create
from code_saturne.cs_create import set_executable, create_local_launcher
from code_saturne import cs_run_conf

from code_saturne.model import XMLengine
from code_saturne.studymanager.cs_studymanager_pathes_model import PathesModel

from code_saturne.studymanager.cs_studymanager_parser import Parser
from code_saturne.studymanager.cs_studymanager_texmaker import Report1, Report2

try:
    from code_saturne.studymanager.cs_studymanager_drawing import Plotter
except Exception:
    print("Warning: import studymanager Plotter failed. Plotting disabled.\n")
    pass

from code_saturne.studymanager.cs_studymanager_run import run_studymanager_command
from code_saturne.studymanager.cs_studymanager_xml_init import smgr_xml_init

#-------------------------------------------------------------------------------
# log config.
#-------------------------------------------------------------------------------

logging.basicConfig()
log = logging.getLogger(__file__)
#log.setLevel(logging.DEBUG)
log.setLevel(logging.NOTSET)

#-------------------------------------------------------------------------------

def nodot(item):
    return item[0] != '.'

#-------------------------------------------------------------------------------

def create_base_xml_file(filepath, pkg):
    """Create studymanager XML file.
    """

    filename = os.path.basename(filepath)
    if os.path.isfile(filepath):
        print("Can not create XML file of parameter:\n" \
              + filepath + " already exists.")
        sys.exit(1)

    # using xml engine from Code_Saturne GUI
    smgr = XMLengine.Case(package=pkg, studymanager=True)
    smgr['xmlfile'] = filename
    pm = PathesModel(smgr)

    # empty repo and dest
    pm.setRepositoryPath('')
    pm.setDestinationPath('')

    return smgr

#-------------------------------------------------------------------------------

def init_xml_file_with_study(smgr, studyp):
    """Initialize XML file with study content and save it.
    """

    smgr_node = smgr.xmlGetNode('studymanager')

    studyd = os.path.basename(studyp)
    st_node = smgr_node.xmlInitChildNode('study', label = studyd)
    st_node['status'] = "on"

    cases = []
    for elt in os.listdir(studyp):
        eltd = os.path.join(studyp, elt)
        if os.path.isdir(eltd):
            if isCase(eltd):
                cases.append(elt)

    cases.sort()
    for case in cases:
        c_node = st_node.xmlInitChildNode("case", label = case)
        c_node['status']  = "on"
        c_node['compute'] = "on"
        c_node['post']    = "on"

    smgr.xmlSaveDocument()

#-------------------------------------------------------------------------------

def isStudy(dirpath):
    """Try to determine if dirpath is a Code_Saturne study directory.
    """

    meshd = os.path.join(dirpath, 'MESH')
    is_study = os.path.isdir(meshd)

    return is_study

#-------------------------------------------------------------------------------

def isCase(dirpath):
    """Try to determine if dirpath is a Code_Saturne case directory.
    """

    # Verify that DATA folder exists with a xml file inside
    datad = os.path.join(dirpath, 'DATA')

    found_xml = False
    if os.path.isdir(datad):
        for elt in os.listdir(datad):
            if ".xml" in str(elt):
                found_xml = os.path.isfile(os.path.join(datad, elt))

    return found_xml

#===============================================================================
# Case class
#===============================================================================

class Case(object):
    def __init__(self, pkg, rlog, diff, parser, study, data, repo, dest):
        """
        @type data: C{Dictionary}
        @param data: contains all keyword and value read in the parameters file
        """
        self.__log       = rlog
        self.__diff      = diff
        self.__parser    = parser
        self.__data      = data
        self.__repo      = repo
        self.__dest      = dest

        self.pkg         = pkg
        self.study       = study

        self.node        = data['node']
        self.label       = data['label']
        self.compute     = data['compute']
        self.plot        = data['post']
        self.run_id      = data['run_id']
        self.compare     = data['compare']
        self.n_procs     = data['n_procs']
        self.depends     = data['depends']

        self.parametric  = data['parametric']
        self.notebook    = data['notebook']
        self.kw_args     = data['kw_args']

        self.is_compiled = "not done"
        self.is_run      = "not done"
        self.is_time     = None
        self.is_plot     = "not done"
        self.is_compare  = "not done"
        self.disabled    = False
        self.threshold   = "default"
        self.diff_value  = [] # list of differences (in case of comparison)
        self.m_size_eq   = True # mesh sizes equal (in case of comparison)
        self.subdomains  = None
        self.run_dir     = ""
        self.level       = None # level of the node in the dependency graph

        self.resu = 'RESU'

        # Title of the case is based on study, label and run_id
        self.title = study + "/" + self.label
        if self.run_id:
            self.title += "/RESU/" + self.run_id

        # Check for coupling
        # TODO: use run.cfg info, so as to allow another coupling parameters
        #       path ('run.cfg' is fixed, 'coupling_parameters.py' is not).

        coupling = False
        run_conf = None
        run_config_path = os.path.join(self.__repo, self.label, "run.cfg")
        if os.path.isfile(run_config_path):
            run_conf = cs_run_conf.run_conf(run_config_path, package=self.pkg)
            if run_conf.get("setup", "coupled_domains") != None:
                coupling = True

        if coupling:
            self.resu = 'RESU_COUPLING'

            # Apply coupling parameters information

            from code_saturne import cs_case_coupling

            coupled_domains = run_conf.get_coupling_parameters()

            self.subdomains = []
            for d in coupled_domains:
                if d['solver'].lower() in ('code_saturne', 'neptune_cfd'):
                    self.subdomains.append(d['domain'])

        self.exe = os.path.join(pkg.get_dir('bindir'),
                                pkg.name + pkg.config.shext)

    #---------------------------------------------------------------------------

    def create(self):
        """
        Create a case
        """
        log_lines = []
        e = os.path.join(self.pkg.get_dir('bindir'), self.exe)
        if self.subdomains:
            os.mkdir(self.label)
            os.chdir(self.label)
            refdir = os.path.join(self.__repo, self.label)
            retval = 1
            resu_coupling = None
            for node in os.listdir(refdir):
                ref = os.path.join(self.__repo, self.label, node)
                if node in self.subdomains:
                    cmd = e + " create --case " + node \
                          + " --quiet --noref --copy-from " \
                          + ref
                    node_retval, t = run_studymanager_command(cmd, self.__log)
                    # negative retcode is kept
                    retval = min(node_retval,retval)
                elif os.path.isdir(ref):
                    shutil.copytree(ref, node, symlinks=True)
                else:
                    shutil.copy2(ref, node)
                    if node == 'run.cfg':
                        temp_run_conf = cs_run_conf.run_conf('run.cfg', package=self.pkg)
                        if temp_run_conf.get('setup', 'coupled_domains'):
                            resu_coupling = 'RESU_COUPLING'

            if resu_coupling and not os.path.isdir(resu_coupling):
                os.mkdir(resu_coupling)

            create_local_launcher(self.pkg, self.__dest)
            os.chdir(self.__dest)
        else:
            cmd = e + " create --case " + self.label  \
                  + " --quiet --noref --copy-from "    \
                  + os.path.join(self.__repo, self.label)
            retval, t = run_studymanager_command(cmd, self.__log)
        if retval == 0:
            log_lines += ['      * create case: ' + self.label]

        else:
            log_lines += ['      * create case: %s --> FAILED' % self.label]

        return log_lines

    #---------------------------------------------------------------------------

    def __update_setup(self, subdir):
        """
        Update setup file in the Repository.
        """
        # Load setup.xml file in order to update it
        # with the __backwardCompatibility method.

        from code_saturne.model.XMLengine import Case

        for fn in os.listdir(os.path.join(self.__repo, subdir, "DATA")):
            fp = os.path.join(self.__repo, subdir, "DATA", fn)
            if os.path.isfile(fp):
                fd = os.open(fp , os.O_RDONLY)
                f = os.fdopen(fd)
                l = f.readline()
                f.close()
                xml_type = None
                if l.startswith('''<?xml version="1.0" encoding="utf-8"?><Code_Saturne_GUI'''):
                    xml_type = 'code_saturne'
                elif l.startswith('''<?xml version="1.0" encoding="utf-8"?><NEPTUNE_CFD_GUI'''):
                    xml_type = 'neptune_cfd'
                else:
                    continue
                try:
                    case = Case(package = self.pkg, file_name = fp)
                except:
                    print("Parameters file reading error.\n")
                    print("This file is not in accordance with XML specifications.\n")
                    sys.exit(1)

                case['xmlfile'] = fp
                case.xmlCleanAllBlank(case.xmlRootNode())

                if xml_type == 'code_saturne':
                    from code_saturne.model.XMLinitialize import XMLinit as cs_solver_xml_init
                    cs_solver_xml_init(case).initialize()
                elif xml_type == 'neptune_cfd':
                    try:
                        from code_saturne.model.XMLinitializeNeptune import XMLinitNeptune as nc_solver_xml_init
                        nc_solver_xml_init(case).initialize()
                    except ImportError:
                        # Avoid completely failing an update of cases with
                        # mixed solver types when neptune_cfd is not available
                        # (will fail if really trying to run those cases)
                        print("Failed updating a neptune_cfd XML file as neptune_cfd is not available.\n")
                        pass

                case.xmlSaveDocument()

    #---------------------------------------------------------------------------

    def update(self):
        """
        Update path for the script in the Repository.
        """
        # Load the setup file in order to update it
        # with the __backwardCompatibility method.

        if self.subdomains:
            cdirs = []
            for d in self.subdomains:
                cdirs.append(os.path.join(self.label, d))
        else:
            cdirs = (self.label,)

        for d in cdirs:
            self.__update_setup(d)

    #---------------------------------------------------------------------------

    def test_compilation(self, study_path, log):
        """
        Test compilation of sources for current case (if some exist).
        @rtype: C{String}
        @return: compilation test status (None if no files to compile).
        """

        if self.subdomains:
            sdirs = []
            for sd in self.subdomains:
                sdirs.append(os.path.join(study_path, self.label, sd, 'SRC'))
        else:
            sdirs = (os.path.join(study_path, self.label, 'SRC'),)

        # compilation test mode
        dest_dir = None

        self.is_compiled = None
        retcode = 0

        # loop over subdomains
        for s in sdirs:
            src_files = files_to_compile(s)

            if len(src_files) > 0:
                self.is_compiled = "OK"
                retcode += compile_and_link(self.pkg, self.pkg.solver, s, dest_dir,
                                            stdout=log, stderr=log)

        if retcode > 0:
            self.is_compiled = "KO"

        return self.is_compiled

    #---------------------------------------------------------------------------

    def __suggest_run_id(self):

        cmd = enquote_arg(self.exe) + " run --suggest-id"
        p = subprocess.Popen(cmd,
                             shell=True,
                             executable=get_shell_type(),
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE,
                             universal_newlines=True)
        i = p.communicate()[0]
        run_id = " ".join(i.split())

        return run_id, os.path.join(self.__dest, self.label, self.resu, run_id)

    #---------------------------------------------------------------------------

    def overwriteDirectories(self, dirs_to_overwrite):
        """
        Overwrite given directories in the Study tree.
        Label of case is an empty string by default.
        """

        for _dir in dirs_to_overwrite:
            ref = os.path.join(self.__repo, self.label, _dir)
            if os.path.isdir(ref):
                dest = os.path.join(self.__dest, self.label, _dir)
                if os.path.isdir(ref):
                    shutil.rmtree(dest)

                try:
                    shutil.copytree(ref, dest)
                except:
                    print("      Error when overwriting folder %s" %dest)

    #---------------------------------------------------------------------------

    def run(self, resource_name=None):
        """
        Check if a run with same result subdirectory name exists
        and launch run if not.
        """
        home = os.getcwd()
        os.chdir(os.path.join(self.__dest, self.label))

        if self.run_id:
            run_id = self.run_id
            run_dir = os.path.join(self.__dest, self.label, self.resu, run_id)

            if os.path.isdir(run_dir):
                if os.path.isfile(os.path.join(run_dir, "error")):
                    self.is_run = "KO"
                    error = 1
                else:
                    self.is_run = "OK"
                    error = 0
                os.chdir(home)

                return error

        else:
            run_id, run_dir = self.__suggest_run_id()

            while os.path.isdir(run_dir):
                run_id, run_dir = self.__suggest_run_id()

        self.run_id  = run_id
        self.run_dir = run_dir

        run_cmd = enquote_arg(self.exe) + " run --id=" + enquote_arg(self.run_id)

        if self.notebook:
            run_cmd += ' --notebook-args ' + self.notebook

        if self.parametric:
            run_cmd += ' --parametric-args ' + '"' + self.parametric + '"'

        if self.kw_args:
            if self.kw_args.find(" ") < 0:
                self.kw_args += " "  # workaround for arg-parser issue
            run_cmd += ' --kw-args ' + '"' + self.kw_args + '"'

        n_procs = self.__data['n_procs']
        if n_procs:
            run_cmd += " -n " + n_procs

        if resource_name:
            run_cmd += " --with-resource=" + resource_name

        error, self.is_time = run_studymanager_command(run_cmd, self.__log)

        if not error:
            self.is_run = "OK"
        else:
            self.is_run = "KO"

        os.chdir(home)

        return error

    #---------------------------------------------------------------------------

    def runCompare(self, studies, r, d, threshold, args, reference=None):
        home = os.getcwd()

        node = None

        if reference:
            result = os.path.join(reference, self.label, self.resu)
        else:
            result = os.path.join(self.__repo, self.label, self.resu)
        # check_dir called again here to get run_id (possibly date-hour)
        repo, msg = self.check_dir(node, result, r, "repo")
        if msg:
            studies.reporting(msg)
        repo = os.path.join(result, repo, 'checkpoint', 'main')
        if not os.path.isfile(repo):
            repo += '.csc'

        result = os.path.join(self.__dest, self.label, self.resu)
        # check_dir called again here to get run_id (possibly date-hour)
        dest, msg = self.check_dir(node, result, d, "dest")
        if msg:
            studies.reporting(msg)
        dest = os.path.join(result, dest, 'checkpoint', 'main.csc')

        cmd = self.__diff + ' ' + repo + ' ' + dest

        self.threshold = "default"
        if threshold != None:
            cmd += ' --threshold ' + threshold
            self.threshold = threshold

        if args != None:
            cmd += (" " + args)
            l = args.split()
            try:
                i = l.index('--threshold')
                self.threshold = l[i+1]
            except:
                pass

        l = subprocess.Popen(cmd,
                             shell=True,
                             executable=get_shell_type(),
                             stdout=subprocess.PIPE,
                             universal_newlines=True).stdout
        lines = l.readlines()

        # list of field differences
        tab = []
        # meshes have same sizes
        m_size_eq = True

        # studymanager compare log only for field of real values
        for i in range(len(lines)):
            # only select line with "Type" (english and french) and ';'
            # since this should be only true for heads of section
            if lines[i].find("Type") != -1 and lines[i].find(";") != -1:
                line = [x.replace("\""," ").strip() for x in lines[i].split(";")]
                name = line[0]
                info = [x.split(":") for x in line[1:]]
                info = [[x[0].strip(),x[1].strip()] for x in info]

                # section with at least 2 informations (location, type) after
                # their name, and of type r (real)
                if len(info) >= 2 and info[1][1] in ['r4', 'r8']:
                    # if next line contains size, this means sizes are different
                    if lines[i+1].find("Taille") != -1 or lines[i+1].find("Size") != -1:
                        m_size_eq = False
                        break
                    else:
                        line = [x.strip() for x in lines[i+1].split(";")]
                        vals = [x.split(":") for x in line]
                        vals = [[x[0].strip(),x[1].strip()] for x in vals]
                        tab.append([name.replace("_", "\_"),
                                    vals[1][1],
                                    vals[2][1],
                                    self.threshold])

        os.chdir(home)

        return tab, m_size_eq

    #---------------------------------------------------------------------------

    def run_ok(self, run_dir):
        """
        Check if a result directory contains an error file
        or if it doesn't contain a summary file
        """
        if not os.path.isdir(run_dir):
            print("Error: the result directory %s does not exist." % run_dir)
            sys.exit(1)

        msg = ""
        ok = True

        f_error = os.path.join(run_dir, 'error')
        if os.path.isfile(f_error):
            ok = False
            msg += "the result directory %s in case %s " \
                   "contains an error file." % (os.path.basename(run_dir),
                                                self.title)

        f_summary = os.path.join(run_dir, 'summary')
        if not os.path.isfile(f_summary):
            ok = False
            msg += "the result directory %s in case %s " \
                   "does not contain any summary file." \
                   % (os.path.basename(run_dir), self.title)

        return ok, msg

    #---------------------------------------------------------------------------

    def check_dir(self, node, result, rep, attr):
        """
        Check coherency between xml file of parameters and repository or
        destination.
        """
        msg = "Warning: "

        if not os.path.isdir(result):
            msg += "the directory %s " \
                   "does not exist." % (result)
            return None, msg

        # 1. The result directory is given
        if rep != "":
            # check if it exists
            rep_f = os.path.join(result, rep)
            if not os.path.isdir(rep_f):
                msg += "the result directory %s " \
                       "does not exist." % (rep_f)
                return None, msg

            run_ok = self.run_ok(rep_f)
            if not run_ok[0]:
                return None, msg+run_ok[1]

        # 2. The result directory must be found/read automatically;
        elif rep == "":
            # check if there is at least one result directory.
            if len(list(filter(nodot, os.listdir(result)))) == 0:
                msg += "there is no result directory in %s." % (result)
                return None, msg

            # if no run_id is specified in the xml file
            # only one result directory allowed in RESU
            if len(list(filter(nodot, os.listdir(result)))) > 1 \
               and self.run_id == "":
                msg += "there are several result directories in %s " \
                       "and no run id specified." % (result)
                return None, msg

            rep = self.run_id
            # if no run_id is specified in the xml file
            # the only result directory present in RESU is taken
            if rep == "":
                rep = list(filter(nodot, os.listdir(result)))[0]

            rep_f = os.path.join(result, rep)
            if not os.path.isdir(rep_f):
                msg += "the result directory %s " \
                       "does not exist." % (rep_f)
                return None, msg

            run_ok = self.run_ok(rep_f)
            if not run_ok[0]:
                return None, msg+run_ok[1]

            # 3. Update the file of parameters with the name of the result directory
            if node:
                self.__parser.setAttribute(node, attr, rep)

        return rep, None

    #---------------------------------------------------------------------------

    def check_dirs(self, node, repo, dest, reference=None):
        """
        Check coherency between xml file of parameters and repository and destination.
        """
        msg = None

        if repo != None:
            # build path to RESU directory with path to study and case label in the repo
            if reference:
                result = os.path.join(reference, self.label, self.resu)
            else:
                result = os.path.join(self.__repo, self.label, self.resu)
            rep, msg = self.check_dir(node, result, repo, "repo")

        if dest != None:
            # build path to RESU directory with path to study and case label in the dest
            result = os.path.join(self.__dest, self.label, self.resu)
            rep, msg = self.check_dir(node, result, dest, "dest")

        return msg

    #---------------------------------------------------------------------------

    def disable(self):

        self.disabled = True
        msg = "    - Case %s --> DISABLED" %(self.title)

        return msg

#===============================================================================
# Study class
#===============================================================================

class Study(object):
    """
    Create, run and compare all cases for a given study.
    """
    def __init__(self, pkg, parser, study, exe, dif, rlog, n_procs=None,
                 with_tags=None, without_tags=None):
        """
        Constructor.
          1. initialize attributes,
          2. build the list of the cases,
          3. build the list of the keywords from the case
        @type parser: C{Parser}
        @param parser: instance of the parser
        @type study: C{String}
        @param study: label of the current study
        @type exe: C{String}
        @param exe: name of the solver executable: C{code_saturne} or C{neptune_cfd}.
        @type dif: C{String}
        @param dif: name of the diff executable: C{cs_io_dump -d}.
        @n_procs: C{int}
        @param n_procs: number of requested processors
        @type with_tags: C{List}
        @param with_tags: list of tags given at the command line
        @type without_tags: C{List}
        @param without_tags: list of tags given at the command line
        """
        # Initialize attributes
        self.__package  = pkg
        self.__parser   = parser
        self.__main_exe = exe
        self.__diff     = dif
        self.__log      = rlog

        # read repository and destination from smgr file
        # based on working directory if information is not available
        try:
            self.__repo = os.path.join(self.__parser.getRepository(),  study)
        except:
            self.__repo = os.path.join(os.getcwd(),  study)
        try:
            self.__dest = os.path.join(self.__parser.getDestination(), study)
        except:
            self.__dest = self.__repo

        if not os.path.isdir(self.__repo):
            print("Error: the directory %s does not exist" % self.__repo)
            sys.exit(1)

        self.label = study

        self.cases = []
        self.matplotlib_figures = []
        self.input_figures = []
        self.case_labels = []

        # get list of cases in study
        on_cases = parser.getStatusOnCasesLabels(study)
        if not on_cases:

            print("\n\n\nWarning: no case defined in %s study\n\n\n" % study)

        else:

            for data in self.__parser.getStatusOnCasesKeywords(self.label):

                # n_procs given in smgr command line overwrites n_procs by case
                # TODO: modify with Yvan development
                if n_procs:
                    data['n_procs'] = str(n_procs)

                # TODO: move tag's filtering to the graph level
                # not done for now as the POST step needs tags also
                # check if every tag passed by option --with-tags belongs to
                # list of tags of the current case
                tagged = False
                if with_tags and data['tags']:
                    tagged = all(tag in data['tags'] for tag in with_tags)
                elif not with_tags:
                    tagged = True

                # check if none of tags passed by option --without-tags
                # belong to list of tags of the current case
                exclude = False
                if without_tags and data['tags']:
                    exclude = any(tag in data['tags'] for tag in without_tags)

                # do not append case if tags do not match
                if tagged and not exclude:
                    c = Case(pkg,
                             self.__log,
                             self.__diff,
                             self.__parser,
                             self.label,
                             data,
                             self.__repo,
                             self.__dest)
                    self.cases.append(c)
                    self.case_labels.append(c.label)

    #---------------------------------------------------------------------------

    def getRunDirectories(self):
        list_cases = []
        list_dir   = []

        for case in self.cases:
            if case.is_run != "KO":
                list_cases.append(case.label)
                list_dir.append(case.run_dir)

        return " ".join(list_cases), " ".join(list_dir)

    #---------------------------------------------------------------------------

    def needs_report_detailed(self, postpro):
        """
        check if study needs a section in the detailed report
        (for figures, comparison or input)
        """
        # study has figures or input figures
        needs = self.matplotlib_figures or self.input_figures

        for case in self.cases:
            if case.is_compare == "done":
                needs = True
                break

            # handle the input nodes that are inside case nodes
            if case.plot == "on" and case.is_run != "KO":
                nodes = self.__parser.getChildren(case.node, "input")
                if nodes:
                    needs = True
                    break

        # handle the input nodes that are inside postpro nodes
        if postpro:
            script, label, nodes, args = self.__parser.getPostPro(self.label)
            for i in range(len(label)):
                if script[i]:
                    input_nodes = self.__parser.getChildren(nodes[i], "input")
                    if input_nodes:
                        needs = True
                        break

        return needs

#===============================================================================
# Studies class
#===============================================================================

class Studies(object):
    """
    Manage all Studies and all Cases described in the files of parameters.
    """
    def __init__(self, pkg, options, exe, dif):
        """
        Constructor.
          1. create if necessary the destination directory,
          2. initialize the parser and the plotter,
          3. build the list of the studies,
          4. start the report.
        @type options: C{Structure}
        @param options: structure the parameters options.
        @type exe: C{String}
        @param exe: name of the solver executable: C{code_saturne} or C{neptune_cfd}.
        @type dif: C{String}
        @param dif: name of the diff executable: C{cs_io_dump -d}.
        """

        # try to determine if current directory is a study one
        cwd = os.getcwd()
        is_study = isStudy(cwd)
        studyd = None
        studyp = None
        if is_study:
            # default study directory is current one
            studyp = cwd

        smgr = None
        self.__pkg = pkg
        self.__create_xml = options.create_xml
        self.__update_smgr = options.update_smgr
        self.__update_setup = options.update_setup

        # Create file of parameters

        filename = options.filename
        if self.__create_xml and is_study:
            if filename == None:
                studyd = os.path.basename(studyp)
                filename = "smgr.xml"

            filepath = os.path.join(studyp, filename)
            smgr = create_base_xml_file(filepath, self.__pkg)

            init_xml_file_with_study(smgr, studyp)
            print(" New smgr.xml file was created")

        elif self.__create_xml and not is_study:
            msg =   "Can not create XML file of parameter:\n" \
                  + "current directory is apparently not a study (no MESH directory).\n"
            sys.exit(msg)

        if filename == None:
            msg =    "A file of parameters must be specified or created " \
                   + "for studymanager to run.\n" \
                   + "See help message and use '--file' or '--create-xml' option.\n"
            sys.exit(msg)

        # create a first smgr parser only for
        #   the repository verification and
        #   the destination creation

        if os.path.isfile(filename):
            self.__parser = Parser(filename)
        else:
            msg = "Specified XML parameter file for studymanager does not exist.\n"
            sys.exit(msg)

        # call smgr xml backward compatibility

        if not smgr:
            smgr = XMLengine.Case(package=self.__pkg, file_name=filename, studymanager=True)
            smgr['xmlfile'] = filename

            # minimal modification of xml for now
            smgr_xml_init(smgr).initialize(reinit_indices = False)
            if self.__update_smgr:
                smgr.xmlSaveDocument(prettyString=False)
                print(" Smgr file was updated")

        # set repository
        if len(options.repo_path) > 0:
            self.__parser.setRepository(options.repo_path)
        self.__repo = self.__parser.getRepository()
        if self.__repo:
            if not os.path.isdir(self.__repo):
                msg="Studies.__init__() >> self.__repo = {0}\n".format(self.__repo)
                sys.exit(msg+"Error: repository path is not valid.\n")
        else: # default value
            # if current directory is a study
            # set repository as directory containing the study
            if is_study:
                studyd = os.path.basename(studyp)
                self.__parser.setRepository(os.path.join(studyp,".."))
                self.__repo = self.__parser.getRepository()
            else:
                msg =   "Can not set a default repository directory:\n" \
                      + "current directory is apparently not a study (no MESH directory).\n" \
                      + "Add a repository path to the parameter file or use the command " \
                      + "line option (--repo=..).\n"
                sys.exit(msg)

        # set destination
        if self.__update_smgr or self.__update_setup or self.__create_xml:
            self.__dest = self.__repo
        else:
            if len(options.dest_path) > 0:
                self.__parser.setDestination(options.dest_path)
            self.__dest = self.__parser.getDestination()
            if not self.__dest: # default value
                # if current directory is a study
                # set destination as a directory "../RUN_(study_name)
                if is_study and studyd != None:
                    self.__parser.setDestination(os.path.join(studyp,
                                                              "../RUN_"+studyd))
                    self.__dest = self.__parser.getDestination()
                else:
                  msg =   "Can not set a default destination directory:\n" \
                        + "current directory is apparently not a study (no MESH directory).\n" \
                        + "Add a destination path to the parameter file or use the command " \
                        + "line option (--dest=..).\n"
                  sys.exit(msg)

        if self.__dest != self.__repo:

          # create if necessary the destination directory

          if not os.path.isdir(self.__dest):
              os.makedirs(self.__dest)

          # copy the smgr file for update and restart

          file = os.path.join(self.__dest, os.path.basename(filename))
          try:
              shutil.copyfile(filename, file)
          except:
              pass

          # create definitive parser for smgr file in destination

          self.__parser  = Parser(file)
          self.__parser.setDestination(self.__dest)
          self.__parser.setRepository(self.__repo)
          if options.debug:
              print(" Studies >> Repository  >> ", self.__repo)
              print(" Studies >> Destination >> ", self.__dest)

        # create plotter

        try:
            self.__plotter = Plotter(self.__parser)
        except Exception:
            self.__plotter = None

        # create list of restricting and excluding tags

        self.__with_tags = None
        if options.with_tags:
            with_tags = re.split(',', options.with_tags)
            self.__with_tags = [tag.strip() for tag in with_tags]
        self.__without_tags = None
        if options.without_tags:
            without_tags = re.split(',', options.without_tags)
            self.__without_tags = [tag.strip() for tag in without_tags]

        # store options
        self.__force_rm       = options.remove_existing
        self.__disable_ow     = options.disable_overwrite
        self.__debug          = options.debug
        self.__n_procs        = options.n_procs
        self.__filter_level   = options.filter_level
        self.__filter_n_procs = options.filter_n_procs

        # Use the provided resource name if forced

        self.__resource_name = options.resource_name

        # build the list of the studies

        doc = os.path.join(self.__dest, options.log_file)
        self.__log = open(doc, "w")
        self.labels  = self.__parser.getStudiesLabel()
        self.studies = []
        for l in self.labels:
            self.studies.append( [l, Study(self.__pkg, self.__parser, l, \
                                           exe, dif, self.__log, \
                                           options.n_procs, \
                                           self.__with_tags, \
                                           self.__without_tags,)] )
            if options.debug:
                print(" >> Append study ", l)

        # attributes

        self.__debug       = options.debug
        self.__quiet       = options.quiet
        self.__running     = options.runcase
        self.__n_iter      = options.n_iterations
        self.__compare     = options.compare
        self.__ref         = options.reference
        self.__postpro     = options.post
        self.__default_fmt = options.default_fmt
        # do not use tex in matplotlib (built-in mathtext is used instead)
        self.__dis_tex     = options.disable_tex
        # tex reports compilation with pdflatex
        self.__pdflatex    = not options.disable_pdflatex

        # start the report

        if options.runcase or options.compare or options.post:
            self.report = os.path.join(self.__dest, "report.txt")
            self.reportFile = open(self.report, mode='w')
            self.reportFile.write('\n')

        # in case of restart

        iok = 0
        for l, s in self.studies:
            for case in s.cases:
                if case.compute == 'on':
                   iok+=1
        if not iok:
            self.__running = False

        if self.__update_smgr or self.__update_setup or self.__create_xml:
            os.remove(doc)

        # Handle relative paths:
        if self.__ref:
            if not os.path.isabs(self.__ref):
                self.__ref = os.path.join(os.getcwd(), self.__ref)

    #---------------------------------------------------------------------------

    def getDestination(self):
        """
        @rtype: C{String}
        @return: destination directory of all studies.
        """
        if self.__dest == None:
            msg=" cs_studymanager_study.py >> Studies.getDestination()"
            msg+=" >> self.__dest is not set"
            sys.exit(msg)
        return self.__dest

    #---------------------------------------------------------------------------

    def getRepository(self):
        """
        @rtype: C{String}
        @return: repository directory of all studies.
        """
        if self.__repo == None:
            msg=" cs_studymanager_study.py >> Studies.getRepository()"
            msg+=" >> self.__repo is not set"
            sys.exit(msg)
        return self.__repo

    #---------------------------------------------------------------------------

    def reporting(self, msg, stdout=True, report=True, status=False):
        """
        Write message on standard output and/or in report.
        @type l: C{String}
        @param l: the sentence to be written.
        """
        s  = ""
        if not status:
            s = chr(10)

        if stdout and not self.__quiet:
            sys.stdout.write (msg + chr(13) + s)
            sys.stdout.flush()

        if report:
            self.reportFile.write(msg + '\n')
            self.reportFile.flush()

    #---------------------------------------------------------------------------

    def report_action_location(self, header_msg, destination=True):
        """
        Add a message to report/stdout at head of sections
        specifying if action is performed in dest or repo.
        """
        if destination:
            header_msg = header_msg + " (in destination)"
        else:
            header_msg = header_msg + " (in repository)"

        self.reporting(header_msg)

    #---------------------------------------------------------------------------

    def updateSetup(self):
        """
        Update setup files in all cases.
        """

        for l, s in self.studies:
            self.reporting('  o In repository: ' + l, report=False)
            for case in s.cases:
                self.reporting('    - update setup file in %s' % case.label,
                               report=False)
                case.update()

        self.reporting('',report=False)

    #---------------------------------------------------------------------------

    def create_studies(self):
        """
        Create all studies and all cases.
        """

        self.reporting("  o Create all studies and cases")
        study_list = []
        case_list = []

        for case in self.graph.graph_dict:

            # first step: create study of the case if necessary
            study = case.study
            if study not in study_list:
                self.create_study(study)
                study_list.append(study)

            # second step: create case if necessary
            case_name = case.study + "/" + case.label
            if case_name not in case_list:
                log_lines = self.create_case(case)
                case_list.append(case_name)
                for line in log_lines:
                    self.reporting(line)

        self.reporting('')

    #---------------------------------------------------------------------------

    def create_study(self, study):

        dest_study = os.path.join(self.__dest, study)
        repo_study = os.path.join(self.__repo, study)

        new_study = False
        os.chdir(self.__dest)
        # Create study if necessary
        if not os.path.isdir(dest_study):
            new_study = True
            # build instance of study class
            cr_study = cs_create.study(self.__pkg, study)    # quiet

            create_msg = "    - Create study " + study
            dest = True
            self.report_action_location(create_msg, dest)

            # TODO: copy-from for study. For now, an empty study
            # is created and cases are created one by one with
            # copy-from

            # create empty study
            cr_study.create()

        # write content of MESH and POST is the study is new
        # overwrite them if not disabled
        if new_study or not self.__disable_ow:

            if not new_study:
                self.reporting("  Warning: MESH, POST, DATA and SRC folder are"
                               " overwritten in %s use option --dow to disable"
                               " overwrite" %study)

            # Link meshes and copy other files
            ref = os.path.join(repo_study, "MESH")
            if os.path.isdir(ref):
                l = os.listdir(ref)
                meshes = []
                for cpr in ["", ".gz"]:
                    for fmt in ["unv",
                                "med",
                                "ccm",
                                "cgns",
                                "neu",
                                "msh",
                                "des"]:
                        meshes += fnmatch.filter(l, "*." + fmt + cpr)
                des = os.path.join(dest_study, "MESH")

                if os.path.isdir(des):
                    shutil.rmtree(des)
                os.mkdir(des)

                for m in l:
                    if m in meshes:
                        if sys.platform.startswith('win'):
                            shutil.copy2(os.path.join(ref, m), os.path.join(des, m))
                        else:
                            os.symlink(os.path.join(ref, m), os.path.join(des, m))
                    elif m != ".svn":
                        t = os.path.join(ref, m)
                        if os.path.isdir(t):
                            shutil.copytree(t, os.path.join(des, m))
                        elif os.path.isfile(t):
                            shutil.copy2(t, des)

            # Copy external scripts for post-processing
            ref = os.path.join(repo_study, "POST")
            if os.path.isdir(ref):
                des = os.path.join(dest_study, "POST")
                shutil.rmtree(des)
                shutil.copytree(ref, des, symlinks=True)

    #---------------------------------------------------------------------------

    def create_case(self, case):

        # Change directory to destination directory
        dest_study = os.path.join(self.__dest, case.study)
        os.chdir(dest_study)

        log_lines = []
        if not os.path.isdir(case.label):
            log_lines = case.create()
        else:
            if self.__force_rm:
                # Build short path to RESU dir. such as 'CASE1/RESU'
                _dest_resu_dir = os.path.join(case.label, 'RESU')
                if os.path.isdir(_dest_resu_dir):
                    if os.listdir(_dest_resu_dir):
                        shutil.rmtree(_dest_resu_dir)
                        os.makedirs(_dest_resu_dir)
                        self.reporting("  All earlier results in case %s/RESU "
                                       "are removed (option --rm activated)"
                                       %case.label)
            else:
               if case.run_id:
                   _dest_resu_dir = os.path.join(case.label, 'RESU', case.run_id)
                   if os.path.isdir(_dest_resu_dir):
                       self.reporting("  Warning: earlier runs in %s won't be "
                                      "overwritten. Use option --rm to overwrite"
                                      " them." %(case.study + "/" + case.label +
                                      "/RESU"))

            # overwrite content of DATA and SRC if not disabled
            if not self.__disable_ow:
                dirs_to_overwrite = ["DATA", "SRC"]
                case.overwriteDirectories(dirs_to_overwrite)
                # update path in gui/run script
                data_subdir = os.path.join(case.label, "DATA")

            _script_dir = os.path.join(self.__repo, case.study, case.label,
                                       'SCRIPTS')
            if os.path.isdir(_script_dir):
                self.reporting("  Warning: SCRIPTS folder exist in %s. "
                               "Please update the case with code_saturne update."
                               %(case.study + "/" + case.label))

        return log_lines

    #---------------------------------------------------------------------------

    def dump_graph(self):
        """
        Dump dependency graph based on all studies and all cases.
        Can be limited to a sub graph is filters an tags are given
        """

        filter_level   = self.__filter_level
        filter_n_procs = self.__filter_n_procs

        self.reporting('  o Dump dependency graph with option : ')
        self.reporting('     - level=' + str(filter_level))
        self.reporting('     - n_procs=' + str(filter_n_procs))

        # create the global graph with all cases of all studies without filtering
        global_graph = dependency_graph()
        for l, s in self.studies:
            for case in s.cases:
                global_graph.add_node(case)

        # extract the sub graph based on filters and tags
        if filter_level is not None or filter_n_procs is not None:

            sub_graph = dependency_graph()
            for node in global_graph.graph_dict:

                # check if the level of the case is the targeted one
                # only effective if filter_level is prescribed
                target_level = True
                if filter_level is not None:
                    target_level = node.level is int(filter_level)

                # check if the number of procs of the case is the targeted one
                # only effective if filter_n_procs is prescribed
                target_n_procs = True
                if filter_n_procs is not None:
                    target_n_procs = node.n_procs is int(filter_n_procs)

                if target_level and target_n_procs:
                    sub_graph.add_node(node)

            self.graph = sub_graph

        else:
            self.graph = global_graph

        self.reporting('')

    #---------------------------------------------------------------------------

    def test_compilation(self):
        """
        Compile sources of all runs with compute attribute at on.
        """
        iko = 0
        for case in self.graph.graph_dict:
            # build case dir. (in repo.)
            study_path = os.path.join(self.__repo, case.study)

            if case.compute == 'on':

                # test compilation (logs are redirected to smgr log file)
                is_compiled = case.test_compilation(study_path, self.__log)

                # report
                if is_compiled == "OK":
                    self.reporting('    - compile %s --> OK' % case.title)
                elif is_compiled == "KO":
                    self.reporting('    - compile %s --> FAILED' % case.title)
                    iko+=1

        self.reporting('')

        if iko:
            self.reporting('Error: compilation failed for %s case(s).\n' % iko)
            sys.exit(1)

    #---------------------------------------------------------------------------

    def prepro(self, case):
        """
        Launch external additional scripts with arguments.
        """
        pre, label, nodes, args = self.__parser.getPrepro(case.node)
        if self.__debug:
            print(" >> prepro ", pre)
            print(" >> label ", label)
            print(" >> nodes ", nodes)
            print(" >> args  ", args)
        for i in range(len(label)):
            if pre[i]:
                # search if the script is in the MESH directory
                # if not, the script is searched in the directories
                # of the current case
                cmd = os.path.join(self.__dest, case.study, "MESH", label[i])
                if self.__debug:
                    print("Path to prepro script ", cmd)
                if not os.path.isfile(cmd):
                    filePath = ""
                    for root, dirs, fs in os.walk(os.path.join(self.__dest,
                                                               case.study,
                                                               case.label)):
                        if label[i] in fs:
                            filePath = root
                            break

                    cmd = os.path.join(filePath, label[i])

                if os.path.isfile(cmd):
                    sc_name = os.path.basename(cmd)
                    # ensure script is executable
                    set_executable(cmd)

                    cmd += " " + args[i]
                    cmd += " -c " + os.path.join(self.__dest, case.study,
                                                 case.label)
                    repbase = os.getcwd()
                    os.chdir(os.path.join(self.__dest, case.study, "MESH"))

                    # Prepro external script often need install python directory
                    # and package python directory: code_saturne or neptune_cfd
                    p_dir = case.pkg.get_dir('pythondir')
                    pkg_dir = case.pkg.get_dir('pkgpythondir')
                    p_dirs = p_dir + ":" + pkg_dir

                    # if package is neptune_cfd, prepro script often needs
                    # code_saturne package python directory
                    cs_pkg_dir = None
                    if case.pkg.name == 'neptune_cfd':
                        cs_pkg_dir = os.path.join(pkg_dir, '../code_saturne')
                        cs_pkg_dir = os.path.normpath(cs_pkg_dir)
                        p_dirs = p_dirs + ":" + cs_pkg_dir

                    retcode, t = run_studymanager_command(cmd,
                                                          self.__log,
                                                          pythondir = p_dirs)
                    stat = "FAILED" if retcode != 0 else "OK"

                    os.chdir(repbase)

                    self.reporting('    - script %s --> %s (%s s)' % (stat, sc_name, t),
                                   stdout=True, report=False)

                    self.reporting('    - script %s --> %s (%s s)' % (stat, cmd, t),
                                   stdout=False, report=True)

                else:
                    self.reporting('    - script %s not found' % cmd)


    #---------------------------------------------------------------------------

    def run(self):
        """
        Update and run all cases.
        Warning, if the markup of the case is repeated in the xml file of parameters,
        the run of the case is also repeated.
        """

        self.reporting("  o Run all cases")

        for case in self.graph.graph_dict:
            self.prepro(case)
            if self.__running:
                if case.compute == 'on' and case.is_compiled != "KO":

                    if self.__n_iter is not None:
                        if case.subdomains:
                            case_dir = os.path.join(self.__dest, case.study, case.label,
                                                        case.subdomains[0], "DATA")
                        else:
                            case_dir = os.path.join(self.__dest, case.study, case.label, "DATA")
                        os.chdir(case_dir)
                        # Create a control_file in each case DATA
                        if not os.path.exists('control_file'):
                            control_file = open('control_file','w')
                            control_file.write("time_step_limit " + str(self.__n_iter) + "\n")
                            # Flush to ensure that control_file content is seen
                            # when control_file is copied to the run directory on all systems
                            control_file.flush()
                            control_file.close

                    self.reporting('    - running %s ...' % case.title,
                                   stdout=True, report=False, status=True)

                    error = case.run(resource_name = self.__resource_name)
                    if case.is_time:
                        is_time = "%s s" % case.is_time
                    else:
                        is_time = "existed already"

                    if not error:
                        if not case.run_id:
                            self.reporting("    - run %s --> Warning suffix"
                                           " is not read" % case.title)

                        self.reporting('    - run %s --> OK (%s)' \
                                       % (case.title, is_time))
                        self.__parser.setAttribute(case.node,
                                                   "compute",
                                                   "off")

                        # update dest="" attribute
                        n1 = self.__parser.getChildren(case.node, "compare")
                        n2 = self.__parser.getChildren(case.node, "script")
                        n3 = self.__parser.getChildren(case.node, "data")
                        n4 = self.__parser.getChildren(case.node, "probe")
                        n5 = self.__parser.getChildren(case.node, "resu")
                        n6 = self.__parser.getChildren(case.node, "input")
                        for n in n1 + n2 + n3 + n4 + n5 + n6:
                            if self.__parser.getAttribute(n, "dest") == "":
                                self.__parser.setAttribute(n, "dest", case.run_id)
                    else:
                        self.reporting('    - run %s --> FAILED (%s)' \
                                       % (case.title, is_time))

                    self.__log.flush()

        self.reporting('')

    #---------------------------------------------------------------------------

    def check_compare(self, destination=True):
        """
        Check coherency between xml file of parameters and repository.
        Stop if you try to make a comparison with a file which does not exist.
        """
        for case in self.graph.graph_dict:
            check_msg = "  o Check compare of case: " + case.title
            self.report_action_location(check_msg, destination)

            # reference directory passed in studymanager command line overwrites
            # destination in all cases (even if compare is defined by a compare
            # markup with a non empty destination)

            ref = None
            if self.__ref:
                ref = os.path.join(self.__ref, case.study)
            cases_to_disable = []

            if case.compare == 'on' and case.is_run != "KO":
                compare, nodes, repo, dest, threshold, args = self.__parser.getCompare(case.node)
                if compare:
                    is_checked = False
                    for i in range(len(nodes)):
                        if compare[i]:
                            is_checked = True
                            if destination == False:
                                dest[i]= None
                            msg = case.check_dirs(nodes[i], repo[i], dest[i], reference=ref)
                            if msg:
                                self.reporting(msg)
                                cases_to_disable.append(case)

                if not compare or not is_checked:
                    node = None
                    repo = ""
                    dest = ""
                    if destination == False:
                        dest = None
                    msg = case.check_dirs(node, repo, dest, reference=ref)
                    if msg:
                        self.reporting(msg)
                        cases_to_disable.append(case)

            for case in cases_to_disable:
                msg = case.disable()
                self.reporting(msg)

        self.reporting('')

    #---------------------------------------------------------------------------

    def compare_case_and_report(self, case, repo, dest, threshold, args, reference=None):
        """
        Compare the results for one computation and report
        """
        case.is_compare = "done"
        diff_value, m_size_eq = case.runCompare(self,
                                                repo, dest,
                                                threshold, args,
                                                reference=reference)

        case.diff_value += diff_value
        case.m_size_eq = case.m_size_eq and m_size_eq

        if args:
            s_args = 'with args: %s' % args
        else:
            s_args = 'default mode'

        if not m_size_eq:
            self.reporting('    - compare %s (%s) --> DIFFERENT MESH SIZES FOUND' % (case.title, s_args))
        elif diff_value:
            self.reporting('    - compare %s (%s) --> DIFFERENCES FOUND' % (case.title, s_args))
        else:
            self.reporting('    - compare %s (%s) --> NO DIFFERENCES FOUND' % (case.title, s_args))


    #---------------------------------------------------------------------------

    def compare(self):
        """
        Compare the results of the new computations with those from the Repository.
        """
        if self.__compare:
            for case in self.graph.graph_dict:
                self.reporting('  o Compare case: ' + case.title)
                # reference directory passed in studymanager command line overwrites
                # destination in all cases (even if compare is defined by a
                # compare markup with a non empty destination)
                ref = None
                if self.__ref:
                    ref = os.path.join(self.__ref, case.study)
                if case.compare == 'on' and case.is_run != "KO":
                    is_compare, nodes, repo, dest, t, args = self.__parser.getCompare(case.node)
                    if is_compare:
                        for i in range(len(nodes)):
                            if is_compare[i]:
                                self.compare_case_and_report(case,
                                                             repo[i],
                                                             dest[i],
                                                             t[i],
                                                             args[i],
                                                             reference=ref)
                    if not is_compare or case.is_compare != "done":
                        repo = ""
                        dest = ""
                        t    = None
                        args = None
                        self.compare_case_and_report(case,
                                                     repo,
                                                     dest,
                                                     t,
                                                     args,
                                                     reference=ref)

        self.reporting('')

    #---------------------------------------------------------------------------

    def check_script(self, destination=True):
        """
        Check coherency between xml file of parameters and repository.
        Stop if you try to run a script with a file which does not exist.
        """
        scripts_checked = False
        for l, s in self.studies:
            # search for scripts to check before
            check_scripts = False
            for case in s.cases:
                script, label, nodes, args, repo, dest = \
                    self.__parser.getScript(case.node)
                if nodes:
                    check_scripts = True
                    break

            if not check_scripts:
                continue

            # if scripts have to be checked
            check_msg = "  o Check scripts of study: " + l
            self.report_action_location(check_msg, destination)

            scripts_checked = True

            cases_to_disable = []
            for case in s.cases:
                script, label, nodes, args, repo, dest = \
                    self.__parser.getScript(case.node)
                for i in range(len(nodes)):
                    if script[i] and case.is_run != "KO":
                        if destination == False:
                            dest[i] = None
                        msg = case.check_dirs(nodes[i], repo[i], dest[i])
                        if msg:
                            self.reporting(msg)
                            cases_to_disable.append(case)

            for case in cases_to_disable:
                msg = case.disable()
                self.reporting(msg)

        if scripts_checked:
            self.reporting('')

        return scripts_checked

    #---------------------------------------------------------------------------

    def scripts(self):
        """
        Launch external additional scripts with arguments.
        """
        for l, s in self.studies:
            self.reporting("  o Run scripts of study: " + l)
            for case in s.cases:
                script, label, nodes, args, repo, dest = self.__parser.getScript(case.node)
                for i in range(len(label)):
                    if script[i] and case.is_run != "KO":
                        cmd = os.path.join(self.__dest, l, "POST", label[i])
                        if os.path.isfile(cmd):
                            sc_name = os.path.basename(cmd)
                            # ensure script is executable
                            set_executable(cmd)

                            cmd += " " + args[i]
                            if repo[i]:
                                r = os.path.join(self.__repo,  l, case.label, "RESU", repo[i])
                                cmd += " -r " + r
                            if dest[i]:
                                d = os.path.join(self.__dest, l, case.label, "RESU", dest[i])
                                cmd += " -d " + d
                            retcode, t = run_studymanager_command(cmd, self.__log)
                            stat = "FAILED" if retcode != 0 else "OK"

                            self.reporting('    - script %s --> %s (%s s)' % (stat, sc_name, t),
                                           stdout=True, report=False)

                            self.reporting('    - script %s --> %s (%s s)' % (stat, cmd, t),
                                           stdout=True, report=False)
                        else:
                            self.reporting('    - script %s not found' % cmd)

        self.reporting('')

    #---------------------------------------------------------------------------

    def postpro(self):
        """
        Launch external additional scripts with arguments.
        """
        for l, s in self.studies:
            # fill results directories and ids for the cases of the current study
            # that were not run by the current studymanager command
            for case in s.cases:
                if case.is_run != "KO":
                    if case.run_dir == "":
                        resu = os.path.join(self.__dest, l, case.label, case.resu)
                        rep, msg = case.check_dir(None, resu, "", "dest")
                        if msg:
                            self.reporting(msg)
                            case.disable()
                        else:
                            case.run_id = rep
                            case.run_dir = os.path.join(resu, rep)

            script, label, nodes, args = self.__parser.getPostPro(l)
            if not label:
                continue

            self.reporting('  o Postprocessing cases of study: ' + l)
            for i in range(len(label)):
                if script[i]:
                    cmd = os.path.join(self.__dest, l, "POST", label[i])
                    if os.path.isfile(cmd):
                        sc_name = os.path.basename(cmd)
                        # ensure script is executable
                        set_executable(cmd)

                        list_cases, list_dir = s.getRunDirectories()
                        cmd += ' ' + args[i] + ' -c "' + list_cases + '" -d "' \
                               + list_dir + '" -s ' + l

                        self.reporting('    - running postpro %s' % sc_name,
                                       stdout=True, report=False, status=True)

                        retcode, t = run_studymanager_command(cmd, self.__log)
                        stat = "FAILED" if retcode != 0 else "OK"

                        self.reporting('    - postpro %s --> %s (%s s)' \
                                       % (stat, sc_name, t),
                                       stdout=True, report=False)

                        self.reporting('    - postpro %s --> %s (%s s)' \
                                       % (stat, cmd, t),
                                       stdout=False, report=True)
                    else:
                        self.reporting('    - postpro %s not found' % cmd)

        self.reporting('')

    #---------------------------------------------------------------------------

    def check_data(self, case, destination=True):
        """
        Check coherency between xml file of parameters and repository
        for data markups of a run.
        """
        for node in self.__parser.getChildren(case.node, "data"):
            plots, file, dest, repo = self.__parser.getResult(node)
            if destination == False:
                dest = None
            msg = case.check_dirs(node, repo, dest)
            if msg:
                self.reporting(msg)
                return False

        return True

    #---------------------------------------------------------------------------

    def check_probes(self, case, destination=True):
        """
        Check coherency between xml file of parameters and repository
        for probes markups of a run.
        """
        for node in self.__parser.getChildren(case.node, "probes"):
            file, dest, fig = self.__parser.getProbes(node)
            if destination == False:
                dest = None
            repo = None
            msg = case.check_dirs(node, repo, dest)
            if msg:
                self.reporting(msg)
                return False

        return True

    #---------------------------------------------------------------------------

    def check_input(self, case, destination=True):
        """
        Check coherency between xml file of parameters and repository
        for probes markups of a run.
        """
        for node in self.__parser.getChildren(case.node, "input"):
            file, dest, repo, tex = self.__parser.getInput(node)
            if destination == False:
                dest = None
            msg = case.check_dirs(node, repo, dest)
            if msg:
                self.reporting(msg)
                return False

        return True

    #---------------------------------------------------------------------------

    def check_plots_and_input(self, destination=True):
        """
        Check coherency between xml file of parameters and repository.
        Stop if you try to make a plot of a file which does not exist.
        """
        for l, s in self.studies:
            check_msg = "  o Check plots and input of study: " + l
            self.report_action_location(check_msg, destination)

            cases_to_disable = []
            for case in s.cases:
                if case.plot == "on" and case.is_run != "KO":

                    if not self.check_data(case, destination):
                        cases_to_disable.append(case)
                        continue

                    if not self.check_probes(case, destination):
                        cases_to_disable.append(case)
                        continue

                    if not self.check_input(case, destination):
                        cases_to_disable.append(case)
                        continue

            for case in cases_to_disable:
                msg = case.disable()
                self.reporting(msg)

        self.reporting('')

    #---------------------------------------------------------------------------

    def plot(self):
        """
        Plot data.
        """
        if self.__plotter:
            for l, s in self.studies:
                if s.cases:
                    self.reporting('  o Plot study: ' + l)
                    self.__plotter.plot_study(l, s,
                                              self.__dis_tex,
                                              self.__default_fmt)

        self.reporting('')

    #---------------------------------------------------------------------------

    def report_input(self, doc2, i_nodes, s_label, c_label=None):
        """
        Add input to report detailed.
        """
        for i_node in i_nodes:
            f, dest, repo, tex = self.__parser.getInput(i_node)
            doc2.appendLine("\\subsubsection{%s}" % f)

            if dest:
                d = dest
                dd = self.__dest
            elif repo:
                d = repo
                dd = self.__repo
            else:
                d = ""
                dd = ""

            if c_label:
                ff = os.path.join(dd, s_label, c_label, 'RESU', d, f)
            else:
                ff = os.path.join(dd, s_label, 'POST', d, f)

            if not os.path.isfile(ff):
                print("\n\nWarning: this file does not exist: %s\n\n" % ff)
            elif ff[-4:] in ('.png', '.jpg', '.pdf') or ff[-5:] == '.jpeg':
                doc2.addFigure(ff)
            elif tex == 'on':
                doc2.addTexInput(ff)
            else:
                doc2.addInput(ff)

    #---------------------------------------------------------------------------

    def build_reports(self, report1, report2):
        """
        @type report1: C{String}
        @param report1: name of the global report.
        @type report2: C{String}
        @param report2: name of the detailed report.
        @rtype: C{List} of C{String}
        @return: list of file to be attached to the report.
        """
        attached_files = []

        if self.__running or self.__compare or self.__postpro:
            # First global report
            doc1 = Report1(self.__dest,
                           report1,
                           self.__log,
                           self.report,
                           self.__parser.write(),
                           self.__pdflatex)

            for l, s in self.studies:
                for case in s.cases:
                    if case.diff_value or not case.m_size_eq:
                        is_nodiff = "KO"
                    else:
                        is_nodiff = "OK"

                    doc1.add_row(case.study,
                                 case.label,
                                 case.is_compiled,
                                 case.is_run,
                                 case.is_time,
                                 case.is_compare,
                                 is_nodiff)

            attached_files.append(doc1.close())

        # Second detailed report
        if self.__compare or self.__postpro:
            doc2 = Report2(self.__dest,
                           report2,
                           self.__log,
                           self.__pdflatex)

            for l, s in self.studies:
                if not s.needs_report_detailed(self.__postpro):
                    continue

                doc2.appendLine("\\section{%s}" % l)

                if s.matplotlib_figures or s.input_figures:
                    doc2.appendLine("\\subsection{Graphical results}")
                    for g in s.matplotlib_figures:
                        doc2.addFigure(g)
                    for g in s.input_figures:
                        doc2.addFigure(g)

                for case in s.cases:
                    if case.is_compare == "done":
                        run_id = None
                        if case.run_id != "":
                            run_id = case.run_id
                        doc2.appendLine("\\subsection{Comparison for case "
                                        "%s (run_id: %s)}"
                                        % (case.label, run_id))
                        if not case.m_size_eq:
                            doc2.appendLine("Repository and destination "
                                            "have apparently not been run "
                                            "with the same mesh (sizes do "
                                            "not match).")
                        elif case.diff_value:
                            doc2.add_row(case.diff_value, l, case.label)
                        elif self.__compare:
                            doc2.appendLine("No difference between the "
                                            "repository and the "
                                            "destination.")

                    # handle the input nodes that are inside case nodes
                    if case.plot == "on" and case.is_run != "KO":
                        nodes = self.__parser.getChildren(case.node, "input")
                        if nodes:
                            doc2.appendLine("\\subsection{Results for "
                                            "case %s}" % case.label)
                            self.report_input(doc2, nodes, l, case.label)

                # handle the input nodes that are inside postpro nodes
                if self.__postpro:
                    script, label, nodes, args = self.__parser.getPostPro(l)

                    needs_pp_input = False
                    for i in range(len(label)):
                        if script[i]:
                            input_nodes = \
                                self.__parser.getChildren(nodes[i], "input")
                            if input_nodes:
                                needs_pp_input = True
                                break

                    if needs_pp_input:
                        doc2.appendLine("\\subsection{Results for "
                                        "post-processing cases}")
                        for i in range(len(label)):
                            if script[i]:
                                input_nodes = \
                                    self.__parser.getChildren(nodes[i], "input")
                                if input_nodes:
                                    self.report_input(doc2, input_nodes, l)

            attached_files.append(doc2.close())

        return attached_files

    #---------------------------------------------------------------------------

    def getlabel(self):
        return self.labels

    #---------------------------------------------------------------------------

    def logs(self):
        try:
            self.reportFile.close()
        except:
            pass
        self.reportFile = open(self.report, mode='r')
        return self.reportFile.read()

    #---------------------------------------------------------------------------

    def __del__(self):
        try:
            self.__log.close()
        except:
            pass
        try:
            self.reportFile.close()
        except:
            pass

#-------------------------------------------------------------------------------
# class dependency_graph
#-------------------------------------------------------------------------------

class dependency_graph(object):

    def __init__(self):
        """ Initializes a dependency graph object to an empty dictionary
        """
        self.graph_dict = {}

    def add_dependency(self, dependency):
        """ Defines dependency between two cases as an edge in the graph
        """
        (node1, node2) = dependency
        # TODO: Add error message as only one dependency is possible
        if node1 in self.graph_dict:
            self.graph_dict[node1].append(node2)
        else:
            self.graph_dict[node1] = [node2]

    def add_node(self, case):
        """ Add a case in the graph if not already there.
            Add a dependency when depends parameters is defined
        """
        if case not in self.graph_dict:
            self.graph_dict[case] = []

            if case.depends:
                for neighbor in self.graph_dict:
                    neighbor_name = neighbor.study + '/' + neighbor.label + '/' \
                                  + neighbor.run_id
                    if neighbor_name == case.depends:
                        # cases with dependency are level > 0 and connected to the dependency
                        self.add_dependency((case, neighbor))
                        case.level = neighbor.level + 1
                        break
                if case.level is None:
                    msg = "Problem in graph construction : dependency " \
                          + case.depends + " is not found.\n"
                    sys.exit(msg)
            else:
                # cases with no dependency are level 0
                case.level = 0

    def nodes(self):
        """ returns the cases of the dependency graph """
        return list(self.graph_dict.keys())

    def dependencies(self):
        """ returns the dependencies between the cases of the graph """
        dependencies = []
        for node in self.graph_dict:
            for neighbor in self.graph_dict[node]:
                if (neighbor, node) not in dependencies:
                    dependencies.append((node, neighbor))
        return dependencies

    def extract_sub_graph(self, filter_level, filter_n_procs):
        """ extracts a sub_graph based on level and n_procs criteria"""
        sub_graph = dependency_graph()
        for node in self.graph_dict:
            keep_node = True
            if filter_level is not None:
                keep_node = node.level is int(filter_level)
            if filter_n_procs is not None:
                keep_node = node.n_procs is int(filter_n_procs)
            if keep_node:
                sub_graph.add_node(node)
        return sub_graph

    def __str__(self):
        res = "\nList of cases: "
        for node in self.nodes():
            res += str(node) + " "
        res += "\nList of dependencies: "
        for dependency in self.dependencies():
            (node1, node2) = dependency
            res += '\n' + str(node1.name) + ' depends on ' + str(node2.name)
        return res

#-------------------------------------------------------------------------------
