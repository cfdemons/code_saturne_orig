/*============================================================================
 * Cartesian mesh generation
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2020 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include <math.h>
#include "bft_error.h"
#include "bft_mem.h"
#include "bft_printf.h"
#include "cs_mesh_builder.h"
#include "cs_mesh_cartesian.h"


BEGIN_C_DECLS

/*============================================================================
 * Structure definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/* parameters for a direction (x, y or z) */
/*----------------------------------------------------------------------------*/

typedef struct {

  /* Law type: Constant, geometric, parabolic or user */
  cs_mesh_cartesian_law_t  law;

  /* Number of cells */
  int                      ncells;

  /* Min and max coordinates */
  cs_real_t                smin;
  cs_real_t                smax;

  /* Progression, used only for geometric or parabolic laws */
  cs_real_t                progression;

  /* Two possibilities :
   *  - If constant law, this is an array of size 1 containing the step
   *  - Else, array of size ncells + 1, containing vertex coordinates.
   */
  cs_real_t               *s;
} _cs_mesh_cartesian_direction_t ;

/*----------------------------------------------------------------------------*/
/* Cartesian mesh parameters structure */
/*----------------------------------------------------------------------------*/

struct _cs_mesh_cartesian_params_t {

  /* Number of direction, set to 3 by default */
  int                             ndir;

  /* Array of size ndir, containing parameters for each direction */
  _cs_mesh_cartesian_direction_t **params;

};

/*============================================================================
 * Private global variables
 *============================================================================*/

static int _build_mesh_cartesian = 0;

static int nvtx_per_face   = 4;

static int nfaces_per_cell = 6;

static cs_mesh_cartesian_params_t *_mesh_params = NULL;

/*============================================================================
 * Private functions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*! \brief Create the mesh parameters structure
 *
 * \param[in] ndir  number of directions
 *
 * \return  pointer to mesh parameters structure.
 */
/*----------------------------------------------------------------------------*/

cs_mesh_cartesian_params_t *
_cs_mesh_cartesian_init(int ndir)
{

  if (_mesh_params != NULL)
    bft_error(__FILE__, __LINE__, 0,
              _("Error: cartesian mesh parameters were allready defined!\n"));


  BFT_MALLOC(_mesh_params, 1, cs_mesh_cartesian_params_t);

  _mesh_params->ndir = ndir;
  BFT_MALLOC(_mesh_params->params, ndir, _cs_mesh_cartesian_direction_t *);

  return _mesh_params;
}

/*----------------------------------------------------------------------------*/
/*! \brief Create parameters for a direction.
 *
 * \param[in] law         1D discreization law : constant, geometric or parabolic
 * \param[in] ncells      Number of cells for this direction
 * \param[in] smin        Min coordinate value for this direction
 * \param[in] smax        Max coordinate value for this direction
 * \param[in] progression Progression value, only used for geometric or
 *                        parabolic laws.
 *
 * \return pointer to direction parameter structure
 */
/*----------------------------------------------------------------------------*/

_cs_mesh_cartesian_direction_t *
_cs_mesh_cartesian_create_direction(cs_mesh_cartesian_law_t law,
                                    int                     ncells,
                                    cs_real_t               smin,
                                    cs_real_t               smax,
                                    cs_real_t               progression)
{

  _cs_mesh_cartesian_direction_t *dirp = NULL;

  if (smax < smin)
    bft_error(__FILE__, __LINE__, 0,
              _("Error: smax < smin in %s\n"), __func__);

  BFT_MALLOC(dirp, 1, _cs_mesh_cartesian_direction_t);

  dirp->ncells = ncells;
  dirp->smin   = smin;
  dirp->smax   = smax;

  dirp->law = law;

  cs_real_t dir_len = smax-smin;

  if (law == CS_MESH_CARTESIAN_CONSTANT_LAW) {
    dirp->progression = -1.;
    BFT_MALLOC(dirp->s, 1, cs_real_t);

    dirp->s[0] = dir_len / dirp->ncells;

  } else if (law == CS_MESH_CARTESIAN_GEOMETRIC_LAW) {
    dirp->progression = progression;
    cs_real_t rho   = dirp->progression;
    cs_real_t rho_n = pow(rho, dirp->ncells);
    cs_real_t dx0   = dir_len * (rho - 1.) / (rho_n - 1.);

    BFT_MALLOC(dirp->s, ncells+1, cs_real_t);

    cs_real_t dx_cur = dx0;
    dirp->s[0] = smin;
    for (int c_id = 0; c_id < ncells; c_id++) {
      dirp->s[c_id+1] = dirp->s[c_id] + dx_cur;
      dx_cur *= rho;
    }

  } else if (law == CS_MESH_CARTESIAN_PARABOLIC_LAW) {
    dirp->progression = progression;

    BFT_MALLOC(dirp->s, ncells+1, cs_real_t);
    cs_real_t rho   = dirp->progression;

    cs_real_t dx0 = 0.;

    /* We need to disguish the case of even or odd number of cells */
    int is_even = (ncells % 2 == 0);
    int np = 0;

    if (is_even) {
      np = ncells / 2;
      cs_real_t rho_np = pow(rho, np);
      dx0 = 0.5 * dir_len * (rho - 1.) / (rho_np - 1.);
    } else {
      np = (ncells - 1) / 2;
      cs_real_t rho_np = pow(rho, np);
      cs_real_t rho_np1 = rho_np * rho;
      dx0 = dir_len * (rho - 1.) / (rho_np1 + rho_np - 2.);
    }

    cs_real_t dx_cur = dx0;
    dirp->s[0]      = smin;
    dirp->s[ncells] = smax;

    for (int i = 0; i < np; i++) {
      dirp->s[i+1] = dirp->s[i] + dx_cur;
      dirp->s[ncells-i-1] = dirp->s[ncells-i] - dx_cur;

      dx_cur *= rho;
    }

  } else {
    bft_error(__FILE__, __LINE__, 0,
              _("Error: Method not yet implemented for '%s'\n"),
              __func__);

  }

  return dirp;

}

/*============================================================================
 * Public C functions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return pointer to cartesian mesh parameters structure
 *
 * \return pointer to cs_mesh_cartesian_params_t structure
 */
/*----------------------------------------------------------------------------*/

cs_mesh_cartesian_params_t *
cs_mesh_cartesian_get_params(void)
{
  return _mesh_params;
}

/*----------------------------------------------------------------------------*/
/*! \brief Create cartesian mesh strucutre
 *
 */
/*----------------------------------------------------------------------------*/

void
cs_mesh_cartesian_create(void)
{

  _cs_mesh_cartesian_init(3);

  _build_mesh_cartesian = 1;

}

/*----------------------------------------------------------------------------*/
/*! \brief Define a simple cartesian mesh with a constant step in all directions
 *
 * \param[in] ncells  Array of size 3 containing number of cells in each direction
 * \param[in] xyz     Array of size 6 containing min values, followed by
 *                    max values for the three directions.
 *
 */
/*----------------------------------------------------------------------------*/

void
cs_mesh_cartesian_define_simple(int        ncells[3],
                                cs_real_t  xyz[6])
{

  cs_mesh_cartesian_params_t *mp = cs_mesh_cartesian_get_params();

  if (mp == NULL)
    mp = _cs_mesh_cartesian_init(3);

  for (int idim = 0; idim < 3; idim++)
    mp->params[idim] =
      _cs_mesh_cartesian_create_direction(CS_MESH_CARTESIAN_CONSTANT_LAW,
                                          ncells[idim],
                                          xyz[idim],
                                          xyz[idim+3],
                                          -1.);

}

/*----------------------------------------------------------------------------*/
/*! \brief Define parameters for a given direction.
 *
 * \param[in] idim         Geometrical direction: 0->X, 1->Y, 2->Z
 * \param[in] law          1D discreization law : constant, geometric or parabolic
 * \param[in] ncells       Number of cells for this direction
 * \param[in] smin         Min coordinate value for this direction
 * \param[in] smax         Max coordinate value for this direction
 * \param[in] progression  Progression value, only used for geometric or
 *                         parabolic laws.
 *
 */
/*----------------------------------------------------------------------------*/

void
cs_mesh_cartesian_define_dir_params(int                     idim,
                                    cs_mesh_cartesian_law_t law,
                                    int                     ncells,
                                    cs_real_t               smin,
                                    cs_real_t               smax,
                                    cs_real_t               progression)
{
  cs_mesh_cartesian_params_t *mp = cs_mesh_cartesian_get_params();

  if (mp == NULL)
    mp = _cs_mesh_cartesian_init(3);

  mp->params[idim] = _cs_mesh_cartesian_create_direction(law,
                                                         ncells,
                                                         smin,
                                                         smax,
                                                         progression);
}

/*----------------------------------------------------------------------------*/
/*! \brief Indicate if a cartesian mesh is to be built.
 *
 * \return 1 if mesh needs to be built, 0 otherwise
 */
/*----------------------------------------------------------------------------*/

int
cs_mesh_cartesian_need_build(void)
{

  int retval = _build_mesh_cartesian;

  return retval;

}

/*----------------------------------------------------------------------------*/
/*! \brief Get number of cells in a given direction.
 *
 * \param[in] idim  Index of direction: 0->X, 1->Y, 2->Z
 *
 * \return Number of cells in corresponding direction (int)
 */
/*----------------------------------------------------------------------------*/

int
cs_mesh_cartesian_get_ncells(int idim)
{

  return _mesh_params->params[idim]->ncells;

}

/*----------------------------------------------------------------------------*/
/*! \brief Build unstructured connectivity needed for partitionning.
 *
 * \param[in] mb    pointer to cs_mesh_builder_t structure
 * \param[in] echo  verbosity flag
 *
 */
/*----------------------------------------------------------------------------*/

void
cs_mesh_cartesian_connectivity(cs_mesh_builder_t *mb,
                               long               echo)
{

  CS_UNUSED(echo);

  cs_mesh_cartesian_params_t *mp = _mesh_params;

  /* Number of cells per direction */
  cs_lnum_t nx = mp->params[0]->ncells;
  cs_lnum_t ny = mp->params[1]->ncells;
  cs_lnum_t nz = mp->params[2]->ncells;

  /* Number of vertices per direction */
  cs_lnum_t nxp1 = nx+1;
  cs_lnum_t nyp1 = ny+1;
  cs_lnum_t nzp1 = nz+1;

  /* Compute global values */
  cs_lnum_t ncells = nx * ny * nz;
  cs_lnum_t nvtx   = nxp1 * nyp1 * nzp1;
  cs_lnum_t nfaces = 3 * ncells + nx*ny + nx*ny + ny*nz;

  mb->n_g_faces = nfaces;
  mb->n_g_face_connect_size = nfaces * 2;

  /* Group id */
  if (mb->cell_gc_id == NULL)
    BFT_MALLOC(mb->cell_gc_id, ncells, int);

  for (cs_lnum_t i = 0; i < ncells; i++)
    mb->cell_gc_id[i] = 7;

  if (mb->face_gc_id == NULL)
    BFT_MALLOC(mb->face_gc_id, nfaces, int);

  for (cs_lnum_t i = 0; i < nfaces; i++)
    mb->face_gc_id[i] = 7;

  /* number of vertices per face array */
  if (mb->face_vertices_idx == NULL)
    BFT_MALLOC(mb->face_vertices_idx, nfaces+1, cs_lnum_t);

  mb->face_vertices_idx[0] = 0;
  for (cs_lnum_t i = 0; i < nfaces; i++)
    mb->face_vertices_idx[i+1] = mb->face_vertices_idx[i] + nvtx_per_face;

  /* Face to cell connectivity using global numbering */
  if (mb->face_cells == NULL)
    BFT_MALLOC(mb->face_cells, 2*nfaces, cs_gnum_t);

  if (mb->face_vertices == NULL)
    BFT_MALLOC(mb->face_vertices, 4*nfaces, cs_gnum_t);
  else
    BFT_REALLOC(mb->face_vertices, 4*nfaces, cs_gnum_t);

  /* Numbering start at 1! */
  cs_lnum_t i0 = 1;
  cs_lnum_t f_id = 0;

  /* X normal faces : (Nx+1)*Ny*Nz faces */
  for (cs_lnum_t k = 0; k < nz; k++) {
    for (cs_lnum_t j = 0; j < ny; j++) {
      for (cs_lnum_t i = 0; i < nxp1; i++) {

        /* Face to cell connectivity */
        cs_lnum_t c0 = i0 + (i-1) + j*nx + k*nx*ny;

        cs_lnum_t c_id1 = 0;
        cs_lnum_t c_id2 = 0;

        if (i == 0) {
          c_id2 = c0 + 1;
          mb->face_gc_id[f_id] = 1;
        }
        else if (i == nx) {
          c_id1 = c0;
          mb->face_gc_id[f_id] = 2;
        }
        else {
          c_id1 = c0;
          c_id2 = c0 + 1;
        }
        mb->face_cells[2*f_id    ] = c_id1;
        mb->face_cells[2*f_id + 1] = c_id2;

        /*  Connectiviy for x-normal faces:
         *
         *  Vtx2        Vtx3
         *  (j,k+1)     (j+1,k+1)
         *
         *   *-----------*       z (k)
         *   |           |       ^
         *   |           |       |
         *   |     *     |       |
         *   |  (i,j,k)  |       .----->y (j)
         *   |           |
         *   *-----------*
         *  Vtx1        Vtx4
         * (j,k)        (j+1,k)
         *
         */
        mb->face_vertices[4*f_id + 3] = i0 + i + j*nxp1     + k*nxp1*nyp1;
        mb->face_vertices[4*f_id + 2] = i0 + i + j*nxp1     + (k+1)*nxp1*nyp1;
        mb->face_vertices[4*f_id + 1] = i0 + i + (j+1)*nxp1 + (k+1)*nxp1*nyp1;
        mb->face_vertices[4*f_id + 0] = i0 + i + (j+1)*nxp1 + k*nxp1*nyp1;

         /* Incerement face index for x-normal faces */
        f_id += 1;
      }
    }
  }

  /* Y normal faces */
  for (cs_lnum_t k = 0; k < nz; k++) {
    for (cs_lnum_t j = 0; j < nyp1; j++) {
      for (cs_lnum_t i = 0; i < nx; i++) {

        cs_lnum_t c_id1 = 0;
        cs_lnum_t c_id2 = 0;

        if (j == 0) {
          c_id2 = i0 + i + j*nx + k*nx*ny;
          mb->face_gc_id[f_id] = 3;
        } else if (j == ny) {
          c_id1 = i0 + i + (j-1)*nx + k*nx*ny;
          mb->face_gc_id[f_id] = 4;
        } else {
          c_id1 = i0 + i + (j-1)*nx + k*nx*ny;
          c_id2 = i0 + i + j*nx     + k*nx*ny;
        }

        mb->face_cells[2*f_id]     = c_id1;
        mb->face_cells[2*f_id + 1] = c_id2;

        /*  Connectiviy for y-normal faces:
         *
         *  Vtx2        Vtx3
         *  (i+1,k)     (i+1,k+1)
         *
         *   *-----------*       x (i)
         *   |           |       ^
         *   |           |       |
         *   |     *     |       |
         *   |  (i,j,k)  |       .----->z (k)
         *   |           |
         *   *-----------*
         *  Vtx1        Vtx4
         * (i,k)        (i,k+1)
         *
         */
        mb->face_vertices[4*f_id + 3] = i0 + i     + j*nxp1 + k*nxp1*nyp1;
        mb->face_vertices[4*f_id + 2] = i0 + (i+1) + j*nxp1 + k*nxp1*nyp1;
        mb->face_vertices[4*f_id + 1] = i0 + (i+1) + j*nxp1 + (k+1)*nxp1*nyp1;
        mb->face_vertices[4*f_id + 0] = i0 + i     + j*nxp1 + (k+1)*nxp1*nyp1;

         /* Incerement face index for y-normal faces */
        f_id += 1;
      }
    }
  }

  /* Z normal faces */
  for (cs_lnum_t k = 0; k < nzp1; k++) {
    for (cs_lnum_t j = 0; j < ny; j++) {
      for (cs_lnum_t i = 0; i < nx; i++) {

        cs_lnum_t c_id1 = 0;
        cs_lnum_t c_id2 = 0;

        if (k == 0) {
          c_id2 = i0 + i + j*nx + k*nx*ny;
          mb->face_gc_id[f_id] = 5;
        } else if (k == nz) {
          c_id1 = i0 + i + j*nx + (k-1)*nx*ny;
          mb->face_gc_id[f_id] = 6;
        } else {
          c_id1 = i0 + i + j*nx + (k-1)*nx*ny;
          c_id2 = i0 + i + j*nx + k*nx*ny;
        }

        mb->face_cells[2*f_id]     = c_id1;
        mb->face_cells[2*f_id + 1] = c_id2;

        /*  Connectiviy for z-normal faces:
         *
         *  Vtx2        Vtx3
         *  (i,j+1)     (i+1,j+1)
         *
         *   *-----------*       y (j)
         *   |           |       ^
         *   |           |       |
         *   |     *     |       |
         *   |  (i,j,k)  |       .----->x (i)
         *   |           |
         *   *-----------*
         *  Vtx1        Vtx4
         * (i,j)        (i+1,j)
         *
         */
        mb->face_vertices[4*f_id + 3] = i0 + i     + j*nxp1     + k*nxp1*nyp1;
        mb->face_vertices[4*f_id + 2] = i0 + i     + (j+1)*nxp1 + k*nxp1*nyp1;
        mb->face_vertices[4*f_id + 1] = i0 + (i+1) + (j+1)*nxp1 + k*nxp1*nyp1;
        mb->face_vertices[4*f_id + 0] = i0 + (i+1) + j*nxp1     + k*nxp1*nyp1;

         /* Incerement face index for y-normal faces */
        f_id += 1;
      }
    }
  }

  if (mb->vertex_coords == NULL) {
    cs_lnum_t vc_size = nvtx*3;
    BFT_MALLOC(mb->vertex_coords, vc_size, cs_real_t);
  }

  /* Vertices coords */
  for (cs_lnum_t k = 0; k < nzp1; k++) {
    for (cs_lnum_t j = 0; j < nyp1; j++) {
      for (cs_lnum_t i = 0; i < nxp1; i++) {
        cs_lnum_t v_id = i + j*nxp1 + k*nxp1*nyp1;

        /* X coord */
        cs_lnum_t ijk[3] = {i,j,k};
        for (int idim = 0; idim < 3; idim++) {
          /* Constant step : xyz[idim]  = xyzmin[idim] + ijk*dx[idim] */
          if (mp->params[idim]->law == CS_MESH_CARTESIAN_CONSTANT_LAW) {
            mb->vertex_coords[3*v_id + idim] =
              mp->params[idim]->smin + ijk[idim] * mp->params[idim]->s[0];
          } else {
            /* Non constant step: We allready stored the vertices in dx,
             * since dx[j+1] - dx[j] = dx of cell j
             */
            mb->vertex_coords[3*v_id + idim] = mp->params[idim]->s[ijk[idim]];
          }
        }
      }
    }
  }

}

/*----------------------------------------------------------------------------*/
/*! \brief Destroy cartesian mesh parameters
 */
/*----------------------------------------------------------------------------*/

void
cs_mesh_cartesian_params_destroy(void)
{

  if (_mesh_params == NULL)
    return;

  for (int i = 0; i < _mesh_params->ndir; i++) {
    BFT_FREE(_mesh_params->params[i]->s);
    BFT_FREE(_mesh_params->params[i]);
  }

  BFT_FREE(_mesh_params);
  _mesh_params = NULL;

}



/*----------------------------------------------------------------------------*/

END_C_DECLS