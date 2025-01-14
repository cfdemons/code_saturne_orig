/*============================================================================
 * Main functions dedicated to groundwater flows
 *============================================================================*/

/* VERS */

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2021 EDF S.A.

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

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include <bft_mem.h>

#include "cs_field.h"
#include "cs_gwf_soil.h"
#include "cs_hodge.h"
#include "cs_log.h"
#include "cs_math.h"
#include "cs_mesh_location.h"
#include "cs_parall.h"
#include "cs_post.h"
#include "cs_reco.h"

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_gwf_tracer.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Local macro definitions
 *============================================================================*/

#define CS_GWF_TRACER_DBG 0

/*============================================================================
 * Structure definitions
 *============================================================================*/

/*============================================================================
 * Static global variables
 *============================================================================*/

static const char _err_empty_tracer[] =
  " Stop execution. The structure related to a tracer is empty.\n"
  " Please check your settings.\n";

/*============================================================================
 * Private function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in time-dependent term of the
 *         simulation of tracer equations.
 *         Case of a fully saturated model.
 *         This function fits the generic prototype of cs_xdef_eval_t
 *
 * \param[in]      n_elts        number of elements to consider
 * \param[in]      elt_ids       list of element ids
 * \param[in]      dense_output  perform an indirection for output (true/false)
 * \param[in]      mesh          pointer to a cs_mesh_t structure
 * \param[in]      connect       pointer to a cs_cdo_connect_t structure
 * \param[in]      quant         pointer to a cs_cdo_quantities_t structure
 * \param[in]      t_eval        time at which one performs the evaluation
 * \param[in]      context       NULL or pointer to a structure cast on-the_fly
 * \param[in, out] result        array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_time_pty4std_sat_tracer(cs_lnum_t                    n_elts,
                             const cs_lnum_t              elt_ids[],
                             bool                         dense_output,
                             const cs_mesh_t             *mesh,
                             const cs_cdo_connect_t      *connect,
                             const cs_cdo_quantities_t   *quant,
                             cs_real_t                    t_eval,
                             void                        *context,
                             cs_real_t                   *result)
{
  CS_UNUSED(mesh);
  CS_UNUSED(connect);
  CS_UNUSED(quant);
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;

  /* Sanity check */

  assert(tc != NULL);

  const short int  *c2s = cs_gwf_get_cell2soil();

  for (cs_lnum_t i = 0; i < n_elts; i++) {

    const cs_lnum_t  c_id = (elt_ids == NULL) ? i : elt_ids[i];
    const cs_lnum_t  id = dense_output ? i : c_id;
    const short int  soil_id = c2s[c_id];
    const cs_real_t  saturated_moisture =
      cs_gwf_soil_get_saturated_moisture(soil_id);

    result[id] = saturated_moisture + tc->rho_kd[soil_id];

  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in time-dependent term of the
 *         simulation of tracer equations.
 *         Case of a fully saturated model.
 *         This function fits the generic prototype of cs_xdef_cell_eval_cw_t
 *
 * \param[in]      cm       pointer to a cs_cell_mesh_t structure
 * \param[in]      t_eval   time at which one performs the evaluation
 * \param[in]      context  pointer to an context structure cast on-the_fly
 * \param[in, out] result   array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_time_pty4std_sat_tracer_cw(const cs_cell_mesh_t    *cm,
                                cs_real_t                t_eval,
                                void                    *context,
                                cs_real_t               *result)
{
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;
  const short int  *c2s = cs_gwf_get_cell2soil();
  const short int  soil_id = c2s[cm->c_id];
  const cs_real_t  saturated_moisture =
    cs_gwf_soil_get_saturated_moisture(soil_id);

  /* Sanity check */

  assert(tc != NULL);

  *result = saturated_moisture + tc->rho_kd[soil_id];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in time-dependent term of the
 *         simulation of tracer equations
 *         This function fits the generic prototype of cs_xdef_eval_t
 *
 * \param[in]      n_elts        number of elements to consider
 * \param[in]      elt_ids       list of element ids
 * \param[in]      dense_output  perform an indirection for output (true/false)
 * \param[in]      mesh          pointer to a cs_mesh_t structure
 * \param[in]      connect       pointer to a cs_cdo_connect_t structure
 * \param[in]      quant         pointer to a cs_cdo_quantities_t structure
 * \param[in]      t_eval        time at which one performs the evaluation
 * \param[in]      context       NULL or pointer to a structure cast on-the_fly
 * \param[in, out] result        array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_time_pty4std_tracer(cs_lnum_t                    n_elts,
                         const cs_lnum_t              elt_ids[],
                         bool                         dense_output,
                         const cs_mesh_t             *mesh,
                         const cs_cdo_connect_t      *connect,
                         const cs_cdo_quantities_t   *quant,
                         cs_real_t                    t_eval,
                         void                        *context,
                         cs_real_t                   *result)
{
  CS_UNUSED(mesh);
  CS_UNUSED(connect);
  CS_UNUSED(quant);
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;

  /* Sanity check */

  assert(tc != NULL);

  const cs_real_t  *theta = tc->l_saturation;
  const short int  *c2s = cs_gwf_get_cell2soil();

  if (elt_ids == NULL)
    for (cs_lnum_t i = 0; i < n_elts; i++)
      result[i] = theta[i] + tc->rho_kd[c2s[i]];

  else { /* Loop on a selection of cells */

    for (cs_lnum_t i = 0; i < n_elts; i++) {
      const cs_lnum_t  c_id = elt_ids[i];
      const cs_lnum_t  id = dense_output ? i : c_id;
      result[id] = theta[c_id] + tc->rho_kd[c2s[c_id]];
    }

  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in time-dependent term of the
 *         simulation of tracer equations
 *         This function fits the generic prototype of cs_xdef_cell_eval_cw_t
 *
 * \param[in]      cm       pointer to a cs_cell_mesh_t structure
 * \param[in]      t_eval   time at which one performs the evaluation
 * \param[in]      context  pointer to an context structure cast on-the_fly
 * \param[in, out] result   array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_time_pty4std_tracer_cw(const cs_cell_mesh_t    *cm,
                            cs_real_t                t_eval,
                            void                    *context,
                            cs_real_t               *result)
{
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;
  const short int  *c2s = cs_gwf_get_cell2soil();

  /* Sanity check */

  assert(tc != NULL);

  *result = tc->l_saturation[cm->c_id] + tc->rho_kd[c2s[cm->c_id]];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in the reaction term for the
 *         simulation of standard tracer equations.
 *         Case of a fully saturated model
 *         This function fits the generic prototype of cs_xdef_cell_eval_t
 *
 * \param[in]      n_elts        number of elements to consider
 * \param[in]      elt_ids       list of element ids
 * \param[in]      dense_output  indirection for output (true or false)
 * \param[in]      mesh          pointer to a cs_mesh_t structure
 * \param[in]      connect       pointer to a cs_cdo_connect_t structure
 * \param[in]      quant         pointer to a cs_cdo_quantities_t structure
 * \param[in]      t_eval        time at which one performs the evaluation
 * \param[in]      context       NULL or pointer to a structure cast on-the_fly
 * \param[in, out] result        array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_reaction_pty4std_sat_tracer(cs_lnum_t                    n_elts,
                                 const cs_lnum_t              elt_ids[],
                                 bool                         dense_output,
                                 const cs_mesh_t             *mesh,
                                 const cs_cdo_connect_t      *connect,
                                 const cs_cdo_quantities_t   *quant,
                                 cs_real_t                    t_eval,
                                 void                        *context,
                                 cs_real_t                   *result)
{
  CS_UNUSED(mesh);
  CS_UNUSED(connect);
  CS_UNUSED(quant);
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;

  /* Sanity check */

  assert(tc != NULL);

  const short int  *c2s = cs_gwf_get_cell2soil();

  for (cs_lnum_t i = 0; i < n_elts; i++) {

    const cs_lnum_t  c_id = (elt_ids == NULL) ? i : elt_ids[i];
    const cs_lnum_t  id = (dense_output) ? i : c_id;
    const short int  s = c2s[c_id];
    const cs_real_t  saturated_moisture = cs_gwf_soil_get_saturated_moisture(s);

    result[id] = (saturated_moisture + tc->rho_kd[s]) * tc->reaction_rate[s];

  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in the reaction term for the
 *         simulation of standard tracer equations.
 *         Case of a fully saturated model.
 *         This function fits the generic prototype of cs_xdef_cell_eval_cw_t
 *
 * \param[in]      cm       pointer to a cs_cell_mesh_t structure
 * \param[in]      t_eval   time at which one performs the evaluation
 * \param[in]      context  NULL or pointer to a structure cast on-the_fly
 * \param[in, out] result   array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_reaction_pty4std_sat_tracer_cw(const cs_cell_mesh_t     *cm,
                                    cs_real_t                 t_eval,
                                    void                     *context,
                                    cs_real_t                *result)
{
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;

  /* Sanity check */

  assert(tc != NULL);

  const short int  *c2s = cs_gwf_get_cell2soil();
  const int s = c2s[cm->c_id];
  const cs_real_t  saturated_moisture = cs_gwf_soil_get_saturated_moisture(s);

  *result = (saturated_moisture + tc->rho_kd[s]) * tc->reaction_rate[s];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in the reaction term for the
 *         simulation of standard tracer equations.
 *         This function fits the generic prototype of cs_xdef_cell_eval_t
 *
 * \param[in]      n_elts        number of elements to consider
 * \param[in]      elt_ids       list of element ids
 * \param[in]      dense_output  indirection for output (true or false)
 * \param[in]      mesh          pointer to a cs_mesh_t structure
 * \param[in]      connect       pointer to a cs_cdo_connect_t structure
 * \param[in]      quant         pointer to a cs_cdo_quantities_t structure
 * \param[in]      t_eval        time at which one performs the evaluation
 * \param[in]      context       NULL or pointer to a structure cast on-the_fly
 * \param[in, out] result        array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_reaction_pty4std_tracer(cs_lnum_t                    n_elts,
                             const cs_lnum_t              elt_ids[],
                             bool                         dense_output,
                             const cs_mesh_t             *mesh,
                             const cs_cdo_connect_t      *connect,
                             const cs_cdo_quantities_t   *quant,
                             cs_real_t                    t_eval,
                             void                        *context,
                             cs_real_t                   *result)
{
  CS_UNUSED(mesh);
  CS_UNUSED(connect);
  CS_UNUSED(quant);
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;

  /* Sanity check */

  assert(tc != NULL);

  const cs_real_t  *theta = tc->l_saturation;
  const short int  *c2s = cs_gwf_get_cell2soil();

  if (elt_ids == NULL) {

    for (cs_lnum_t i = 0; i < n_elts; i++) {
      const int  s = c2s[i];  /* soil_id */
      result[i] = (theta[i] + tc->rho_kd[s]) * tc->reaction_rate[s];
    }

  }
  else {

    for (cs_lnum_t i = 0; i < n_elts; i++) {

      const cs_lnum_t  c_id = elt_ids[i];
      const int  s = c2s[c_id];
      const cs_lnum_t  id = (dense_output) ? i : c_id;

      result[id] = (theta[c_id] + tc->rho_kd[s]) * tc->reaction_rate[s];

    }

  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define the coefficient appearing in the reaction term for the
 *         simulation of standard tracer equations.
 *         This function fits the generic prototype of cs_xdef_cell_eval_cw_t
 *
 * \param[in]      cm       pointer to a cs_cell_mesh_t structure
 * \param[in]      t_eval   time at which one performs the evaluation
 * \param[in]      context  NULL or pointer to a structure cast on-the_fly
 * \param[in, out] result   array storing the result (must be allocated)
 */
/*----------------------------------------------------------------------------*/

static inline void
_get_reaction_pty4std_tracer_cw(const cs_cell_mesh_t     *cm,
                                cs_real_t                 t_eval,
                                void                     *context,
                                cs_real_t                *result)
{
  CS_UNUSED(t_eval);

  const cs_gwf_tracer_context_t  *tc = context;

  /* Sanity check */

  assert(tc != NULL);

  const short int  *c2s = cs_gwf_get_cell2soil();
  const int s = c2s[cm->c_id];

  *result = (tc->l_saturation[cm->c_id] + tc->rho_kd[s]) * tc->reaction_rate[s];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Update physical properties for a (non-user) tracer model.
 *         Only the diffusivity is updated (reaction property and time
 *         property are defined by function).
 *         Case of a fully saturated model.
 *         Generic function relying on the prototype cs_gwf_tracer_update_t
 *
 * \param[in, out] tracer     pointer to a cs_gwf_tracer_structure
 * \param[in]      t_eval     time at which one performs the evaluation
 * \param[in]      mesh       pointer to a cs_mesh_t structure
 * \param[in]      connect    pointer to a cs_cdo_connect_t structure
 * \param[in]      quant      pointer to a cs_cdo_quantities_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_update_sat_diff_pty(cs_gwf_tracer_t             *tracer,
                     cs_real_t                    t_eval,
                     const cs_mesh_t             *mesh,
                     const cs_cdo_connect_t      *connect,
                     const cs_cdo_quantities_t   *quant)
{
  CS_UNUSED(mesh);
  CS_UNUSED(connect);
  CS_UNUSED(quant);
  CS_UNUSED(t_eval);

  assert(tracer != NULL);
  if (tracer->diffusivity == NULL)
    return;

  cs_real_t  *values = tracer->diffusivity->val;
  cs_gwf_tracer_context_t  *tc = tracer->context;

  /* Sanity check */

  assert(tc != NULL);

  const cs_real_t  *velocity = tc->darcy_velocity_field->val;

  const int  n_soils = cs_gwf_get_n_soils();
  for (int soil_id = 0; soil_id < n_soils; soil_id++) {

    cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);

    const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);
    const double  wmd = tc->wmd[soil_id];
    const double  at = tc->alpha_t[soil_id];
    const double  al = tc->alpha_l[soil_id];
    const double  theta_s = cs_gwf_soil_get_saturated_moisture(soil_id);

    for (cs_lnum_t i = 0; i < z->n_elts; i++) {

      const cs_lnum_t  c_id = (z->elt_ids == NULL) ? i : z->elt_ids[i];
      const cs_real_t  *v = velocity + 3*c_id;
      const double  v2[3] = {v[0]*v[0], v[1]*v[1], v[2]*v[2]};
      const double  vnorm = sqrt(v2[0] + v2[1] + v2[2]);
      const double  coef1 = wmd * theta_s + at*vnorm;

      double  delta = 0.;
      if (vnorm > cs_math_zero_threshold)
        delta = (al - at)/vnorm;

      const double  dcv[3] = {delta*v[0], delta*v[1], delta*v[2]};

      cs_real_t  *_r = values + 9*c_id;
      for (int ki = 0; ki < 3; ki++) {

        /* Diagonal terms */

        _r[3*ki+ki] = coef1 + delta*v2[ki];

        /* Extra-diagonal terms */

        for (int kj = ki + 1; kj < 3; kj++) {
          _r[3*ki+kj] = dcv[ki]*v[kj];
          _r[3*kj+ki] = _r[3*ki+kj]; /* tensor is symmetric by construction */
        }

      }

    } /* Loop on cells attached to this soil */

  } /* Loop on soils */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Update physical properties for a (non-user) tracer model.
 *         Only the diffusivity is updated (reaction property and time
 *         property are defined by function).
 *         Generic function relying on the prototype cs_gwf_tracer_update_t
 *
 * \param[in, out] tracer     pointer to a cs_gwf_tracer_structure
 * \param[in]      t_eval     time at which one performs the evaluation
 * \param[in]      mesh       pointer to a cs_mesh_t structure
 * \param[in]      connect    pointer to a cs_cdo_connect_t structure
 * \param[in]      quant      pointer to a cs_cdo_quantities_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_update_diff_pty(cs_gwf_tracer_t             *tracer,
                 cs_real_t                    t_eval,
                 const cs_mesh_t             *mesh,
                 const cs_cdo_connect_t      *connect,
                 const cs_cdo_quantities_t   *quant)
{
  CS_UNUSED(mesh);
  CS_UNUSED(connect);
  CS_UNUSED(quant);
  CS_UNUSED(t_eval);

  assert(tracer != NULL);
  if (tracer->diffusivity == NULL)
    return;

  cs_real_t  *values = tracer->diffusivity->val;
  cs_gwf_tracer_context_t  *tc = tracer->context;

  /* Sanity check */

  assert(tc != NULL);

  const cs_real_t  *theta = tc->l_saturation;
  const cs_real_t  *velocity = tc->darcy_velocity_field->val;

  const int  n_soils = cs_gwf_get_n_soils();
  for (int soil_id = 0; soil_id < n_soils; soil_id++) {

    cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);

    const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);
    const double  wmd = tc->wmd[soil_id];
    const double  at = tc->alpha_t[soil_id];
    const double  al = tc->alpha_l[soil_id];

    for (cs_lnum_t i = 0; i < z->n_elts; i++) {

      const cs_lnum_t  c_id = (z->elt_ids == NULL) ? i : z->elt_ids[i];
      const cs_real_t  *v = velocity + 3*c_id;
      const double  v2[3] = {v[0]*v[0], v[1]*v[1], v[2]*v[2]};
      const double  vnorm = sqrt(v2[0] + v2[1] + v2[2]);
      const double  coef1 = wmd * theta[c_id] + at*vnorm;

      double  delta = 0.;
      if (vnorm > cs_math_zero_threshold)
        delta = (al - at)/vnorm;

      const double  dcv[3] = {delta*v[0], delta*v[1], delta*v[2]};

      cs_real_t  *_r = values + 9*c_id;
      for (int ki = 0; ki < 3; ki++) {

        /* Diagonal terms */

        _r[3*ki+ki] = coef1 + delta*v2[ki];

        /* Extra-diagonal terms */

        for (int kj = ki + 1; kj < 3; kj++) {
          _r[3*ki+kj] = dcv[ki]*v[kj];
          _r[3*kj+ki] = _r[3*ki+kj]; /* tensor is symmetric by construction */
        }

      }

    } /* Loop on cells attached to this soil */

  } /* Loop on soils */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Update physical properties for a non-user tracer model.
 *         Case of a tracer with the precipitation/dissolution modelling and
 *         a vertex-based scheme.
 *         Generic function relying on the prototype cs_gwf_tracer_update_t
 *
 * \param[in, out] tracer     pointer to a cs_gwf_tracer_structure
 * \param[in]      t_eval     time at which one performs the evaluation
 * \param[in]      mesh       pointer to a cs_mesh_t structure
 * \param[in]      connect    pointer to a cs_cdo_connect_t structure
 * \param[in]      quant      pointer to a cs_cdo_quantities_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_update_precipitation_vb(cs_gwf_tracer_t             *tracer,
                         cs_real_t                    t_eval,
                         const cs_mesh_t             *mesh,
                         const cs_cdo_connect_t      *connect,
                         const cs_cdo_quantities_t   *quant)
{
  CS_UNUSED(mesh);
  CS_UNUSED(t_eval);

  cs_gwf_tracer_context_t  *tc = tracer->context;

  /* Sanity checks */

  assert(tc->conc_satura != NULL && tc->conc_precip != NULL);

  /* Retrieve the current values of the concentration of tracer in the liquid
     phase */

  cs_real_t  *c_w = cs_equation_get_vertex_values(tracer->eq, false);
  cs_real_t  *c_p = tc->conc_precip;
  cs_real_t  *c_w_save = NULL;

  BFT_MALLOC(c_w_save, quant->n_vertices, cs_real_t);
  memcpy(c_w_save, c_w, quant->n_vertices*sizeof(cs_real_t));

  const cs_adjacency_t  *c2v = connect->c2v;
  const cs_real_t  *theta = tc->l_saturation;

  /* 2) Update c_w and c_p */
  /*    ------------------ */

  const int  n_soils = cs_gwf_get_n_soils();
  for (int soil_id = 0; soil_id < n_soils; soil_id++) {

    cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);

    const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);
    const double  rho = tc->rho_bulk[soil->id];
    const double  inv_rho = 1./rho;

    for (cs_lnum_t i = 0; i < z->n_elts; i++) { /* Loop on cells */

      const cs_lnum_t  c_id = (z->elt_ids == NULL) ? i : z->elt_ids[i];
      const cs_real_t  theta_c = theta[c_id], inv_theta_c = 1./theta_c;

      for (cs_lnum_t j = c2v->idx[c_id]; j < c2v->idx[c_id+1]; j++) {

        const cs_lnum_t  v_id = c2v->ids[j];
        const cs_real_t  c_sat = tc->conc_satura[v_id];

        if (c_w_save[v_id] <= c_sat && c_p[j] > 0) { /* Dissolution */

          cs_real_t  c_w_max = CS_MIN(c_sat,
                                      c_w_save[v_id] + rho*inv_theta_c*c_p[j]);
          c_p[j] -= theta_c*inv_rho*(c_w_max - c_w_save[v_id]);
          c_w[v_id] = CS_MAX(c_w[v_id], c_w_max);

        }
        else if (c_w_save[v_id] > c_sat) { /* Precipitation */

          c_p[j] += theta_c*inv_rho*(c_w_save[v_id] - c_sat);
          c_w[v_id] = c_sat;

        }

      } /* Loop on cell vertices */

    } /* Loop on cells attached to this soil */

  } /* Loop on soils */

  /* Parallel synchronization (in case of dissolution) */

  if (connect->interfaces[CS_CDO_CONNECT_VTX_SCAL] != NULL)
    cs_interface_set_max(connect->interfaces[CS_CDO_CONNECT_VTX_SCAL],
                         quant->n_vertices,
                         1,             /* stride */
                         false,         /* interlace (not useful here) */
                         CS_REAL_TYPE,
                         c_w);

  /* 3) Update the value of concentration in precipitate in each cell */
  /*    ------------------------------------------------------------- */

  cs_real_t  *field_val = tc->precip_field->val;
  for (cs_lnum_t c_id = 0; c_id < quant->n_cells; c_id++) {

    field_val[c_id] = 0;

    for (cs_lnum_t j = c2v->idx[c_id]; j < c2v->idx[c_id+1]; j++)
      field_val[c_id] += quant->dcell_vol[j]*c_p[j];
    field_val[c_id] = field_val[c_id]/quant->cell_vol[c_id];

  } /* Loop on cells */

  /* Free temporary buffer */

  BFT_FREE(c_w_save);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free the context related to a standard tracer equation
 *         Rely on the generic prototype cs_gwf_tracer_free_context_t
 *
 * \param[in, out] tracer     pointer to a structure cs_gwf_tracer_t
 */
/*----------------------------------------------------------------------------*/

static void
_free_default_tracer_context(cs_gwf_tracer_t   *tracer)
{
  cs_gwf_tracer_context_t  *tc = tracer->context;

  if (tc == NULL)
    return;

  BFT_FREE(tc->rho_kd);
  BFT_FREE(tc->alpha_l);
  BFT_FREE(tc->alpha_t);
  BFT_FREE(tc->wmd);
  BFT_FREE(tc->reaction_rate);

  /* Sorption phenomena */

  if (tracer->model & CS_GWF_TRACER_SORPTION_EK_3_PARAMETERS ||
      tracer->model & CS_GWF_TRACER_SORPTION_EK_5_PARAMETERS) {

    BFT_FREE(tc->k0_plus);
    BFT_FREE(tc->k0_minus);
    BFT_FREE(tc->conc_site2);

  }

  /* Precipitation phenomena */

  if (tracer->model & CS_GWF_TRACER_PRECIPITATION) {

    BFT_FREE(tc->conc_w_star);
    BFT_FREE(tc->conc_precip);
    BFT_FREE(tc->conc_satura);

  }

  BFT_FREE(tc);
  tracer->context = NULL;

  /* All fields are freed thanks to another mechanism */
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Add quantities related to the precipitation model
 *
 * \param[in]      connect       pointer to a cs_cdo_connect_t structure
 * \param[in]      quant         pointer to a cs_cdo_quantities_t structure
 * \param[in, out] tracer        pointer to a cs_gwf_tracer_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_add_precipitation(const cs_cdo_connect_t      *connect,
                   const cs_cdo_quantities_t   *quant,
                   cs_gwf_tracer_t             *tracer)
{
  cs_gwf_tracer_context_t  *tc = tracer->context;

  const int  n_soils = cs_gwf_get_n_soils();
  const cs_adjacency_t  *c2v = connect->c2v;
  const cs_param_space_scheme_t  space_scheme =
    cs_equation_get_space_scheme(tracer->eq);

  cs_lnum_t  a_size = 0;

  switch (space_scheme) {

  case CS_SPACE_SCHEME_CDOVB:
    a_size = c2v->idx[quant->n_cells];
    break;

  case CS_SPACE_SCHEME_CDOVCB:
    a_size = c2v->idx[quant->n_cells] + quant->n_cells;
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, "%s: Invalid space scheme.", __func__);

  }

  BFT_MALLOC(tc->conc_precip, a_size, cs_real_t);
  memset(tc->conc_precip, 0, a_size*sizeof(cs_real_t));

  /* Build conc_satura */

  if (space_scheme == CS_SPACE_SCHEME_CDOVCB ||
      space_scheme == CS_SPACE_SCHEME_CDOVB) {

    BFT_MALLOC(tc->conc_satura, quant->n_vertices, cs_real_t);

    for (int soil_id = 0; soil_id < n_soils; soil_id++) {

      cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);

      const double  c_sat = tc->conc_w_star[soil->id];

      if (soil_id == 0) {     /* Initialize with c_sat */

#       pragma omp parallel for if (quant->n_vertices > CS_THR_MIN)
        for (cs_lnum_t v = 0; v < quant->n_vertices; v++)
          tc->conc_satura[v] = c_sat;

      }
      else  {

        const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);
        assert(z->elt_ids != NULL);

        for (cs_lnum_t i = 0; i < z->n_elts; i++) { /* Loop on cells */

          const cs_lnum_t  c_id = z->elt_ids[i];
          for (cs_lnum_t j = c2v->idx[c_id]; j < c2v->idx[c_id+1]; j++) {

            const cs_lnum_t  v_id = c2v->ids[j];

            tc->conc_satura[v_id] = CS_MIN(tc->conc_satura[v_id], c_sat);

          } /* Loop on cell vertices */

        } /* Loop on cells belonging to a soil */

      } /* soild_id > 0 */

    } /* Loop on soils */

  } /* space scheme with DoFs at vertices */

  /* Interface synchronization */

  if (connect->interfaces[CS_CDO_CONNECT_VTX_SCAL] != NULL)
    cs_interface_set_min(connect->interfaces[CS_CDO_CONNECT_VTX_SCAL],
                         quant->n_vertices,
                         1,             /* stride */
                         false,         /* interlace (not useful here) */
                         CS_REAL_TYPE,
                         tc->conc_satura);

}

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Create a new cs_gwf_tracer_t structure and initialize its members by
 *         default.
 *         Add a new equation related to the groundwater flow module.
 *         This equation is a specific transport equation.
 *         Tracer is advected thanks to the darcian velocity which is given
 *         by the resolution of the Richards equation.
 *         Diffusion/reaction parameters result from a physical modelling.
 *
 * \param[in]   tracer_id   id number of the soil
 * \param[in]   eq_name     name of the tracer equation
 * \param[in]   var_name    name of the related variable
 * \param[in]   adv_field   pointer to a cs_adv_field_t structure
 * \param[in]   model       model related to this tracer
 *
 * \return a pointer to the new allocated structure
 */
/*----------------------------------------------------------------------------*/

cs_gwf_tracer_t *
cs_gwf_tracer_init(int                      tracer_id,
                   const char              *eq_name,
                   const char              *var_name,
                   cs_adv_field_t          *adv_field,
                   cs_gwf_tracer_model_t    model)
{
  cs_gwf_tracer_t  *tracer = NULL;

  BFT_MALLOC(tracer, 1, cs_gwf_tracer_t);

  tracer->id = tracer_id;
  tracer->eq = cs_equation_add(eq_name,
                               var_name,
                               CS_EQUATION_TYPE_GROUNDWATER,
                               1, /* scalar-valued equation */
                               CS_PARAM_BC_HMG_NEUMANN);

  tracer->model = model;
  tracer->context = NULL;
  tracer->diffusivity = NULL;
  tracer->reaction_id = -1;

  /* Add a new property related to the time-depedent term */
  char  *pty_name = NULL;
  int  len = strlen(eq_name) + strlen("_time") + 1;
  BFT_MALLOC(pty_name, len, char);
  sprintf(pty_name, "%s_time", eq_name);

  cs_property_t  *time_pty = cs_property_add(pty_name, CS_PROPERTY_ISO);

  BFT_FREE(pty_name);

  cs_equation_param_t  *tr_eqp = cs_equation_get_param(tracer->eq);

  cs_equation_add_time(tr_eqp,  time_pty);

  /* Associate the advection field for the advection term */

  assert(adv_field != NULL); /* Sanity check */
  cs_equation_add_advection(tr_eqp, adv_field);

  cs_equation_param_set(tr_eqp, CS_EQKEY_SPACE_SCHEME, "cdo_vb");

  /* Space discretization */

  cs_equation_param_set(tr_eqp, CS_EQKEY_HODGE_TIME_ALGO, "wbs");
  cs_equation_param_set(tr_eqp, CS_EQKEY_HODGE_REAC_ALGO, "wbs");

  /* Linear algebra */

  cs_equation_param_set(tr_eqp, CS_EQKEY_ITSOL, "gcr");
  cs_equation_param_set(tr_eqp, CS_EQKEY_PRECOND, "poly1");
  cs_equation_param_set(tr_eqp, CS_EQKEY_ADV_SCHEME, "sg");

  const int  n_soils = cs_gwf_get_n_soils();

  if ((model & CS_GWF_TRACER_USER) == 0) {

    cs_gwf_tracer_context_t  *context = NULL;

    BFT_MALLOC(context, 1, cs_gwf_tracer_context_t);

    BFT_MALLOC(context->rho_bulk, n_soils, double);
    BFT_MALLOC(context->kd0, n_soils, double);
    BFT_MALLOC(context->rho_kd, n_soils, double);
    BFT_MALLOC(context->alpha_l, n_soils, double);
    BFT_MALLOC(context->alpha_t, n_soils, double);
    BFT_MALLOC(context->wmd, n_soils, double);
    BFT_MALLOC(context->reaction_rate, n_soils, double);

    context->darcy_velocity_field = NULL;
    context->l_saturation = NULL;

    /* Sorption members */

    context->k0_plus = NULL;
    context->k0_minus = NULL;
    context->conc_site2 = NULL;

    if (model & CS_GWF_TRACER_SORPTION_EK_3_PARAMETERS) {

      BFT_MALLOC(context->k0_minus, n_soils, double);
      BFT_MALLOC(context->k0_plus, n_soils, double);

    }

    /* Precipitation members */

    context->conc_w_star = NULL;
    context->conc_precip = NULL;
    context->conc_satura = NULL;
    context->precip_field = NULL;
    tracer->update_precipitation = NULL;

    if (model & CS_GWF_TRACER_PRECIPITATION) {

      BFT_MALLOC(context->conc_w_star, n_soils, double);

      tracer->update_precipitation = _update_precipitation_vb;

    }

    /* Common to all default tracer */

    tracer->update_diff_tensor = _update_diff_pty;
    tracer->free_context = _free_default_tracer_context;
    tracer->context = context;

  }

  return tracer;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free a cs_gwf_tracer_t structure
 *
 * \param[in, out]  tracer   pointer to a cs_gwf_tracer_t structure
 *
 * \return a NULL pointer
 */
/*----------------------------------------------------------------------------*/

cs_gwf_tracer_t *
cs_gwf_tracer_free(cs_gwf_tracer_t     *tracer)
{
  if (tracer == NULL)
    return tracer;

  if (tracer->free_context != NULL)
    tracer->free_context(tracer);

  /* Tracer equation is freed with all equations at the same time */

  BFT_FREE(tracer);

  return NULL;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set a tracer for a specified soil when the tracer is attached to
 *         the default model
 *
 * \param[in, out] tracer          pointer to a cs_gwf_tracer_t structure
 * \param[in]      soil_name       name of the related soil (or NULL if all
 *                                 soils are selected)
 * \param[in]      wmd             value of the water molecular diffusivity
 * \param[in]      alpha_l         value of the longitudinal dispersivity
 * \param[in]      alpha_t         value of the transversal dispersivity
 * \param[in]      distrib_coef    value of the distribution coefficient
 * \param[in]      reaction_rate   value of the first order rate of reaction
 */
/*----------------------------------------------------------------------------*/

void
cs_gwf_set_main_tracer_param(cs_gwf_tracer_t   *tracer,
                             const char        *soil_name,
                             double             wmd,
                             double             alpha_l,
                             double             alpha_t,
                             double             distrib_coef,
                             double             reaction_rate)
{
  /* Sanity check */
  if (tracer == NULL) bft_error(__FILE__, __LINE__, 0, _(_err_empty_tracer));

  cs_gwf_tracer_context_t  *tc = tracer->context;

  /* Look for the related soil */

  if (soil_name == NULL) { /* All soils have to be set for this tracer */

    const int n_soils = cs_gwf_get_n_soils();
    for (int soil_id = 0; soil_id < n_soils; soil_id++) {

      cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);
      assert(soil != NULL);

      tc->rho_bulk[soil_id] = soil->bulk_density;
      tc->kd0[soil_id] = distrib_coef;
      tc->rho_kd[soil_id] = soil->bulk_density * distrib_coef;
      tc->alpha_l[soil_id] = alpha_l;
      tc->alpha_t[soil_id] = alpha_t;
      tc->wmd[soil_id] = wmd;
      tc->reaction_rate[soil_id] = reaction_rate;

    } /* Loop on soils */

  }
  else { /* Set this tracer equation for a specific soil */

    cs_gwf_soil_t  *soil = cs_gwf_soil_by_name(soil_name);
    if (soil == NULL)
      bft_error(__FILE__, __LINE__, 0,
                " Soil %s not found among the predefined soils.\n"
                " Please check your settings.", soil_name);

    tc->rho_bulk[soil->id] = soil->bulk_density;
    tc->kd0[soil->id] = distrib_coef;
    tc->rho_kd[soil->id] = soil->bulk_density * distrib_coef;
    tc->alpha_l[soil->id] = alpha_l;
    tc->alpha_t[soil->id] = alpha_t;
    tc->wmd[soil->id] = wmd;
    tc->reaction_rate[soil->id] = reaction_rate;

  } /* Set a specific couple (tracer, soil) */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  For a specified soil set the parameters corresponding to a
 *         precipitation modelling of a tracer transport
 *
 * \param[in, out] tracer          pointer to a cs_gwf_tracer_t structure
 * \param[in]      soil_name       name of the related soil (or NULL if all
 *                                 soils are selected)
 * \param[in]      conc_w_star     value of the saturated concentration in the
 *                                 liquid phase
 */
/*----------------------------------------------------------------------------*/

void
cs_gwf_set_precip_tracer_param(cs_gwf_tracer_t   *tracer,
                               const char        *soil_name,
                               double             conc_w_star)
{
  /* Sanity check */
  if (tracer == NULL) bft_error(__FILE__, __LINE__, 0, _(_err_empty_tracer));

  if ((tracer->model & CS_GWF_TRACER_PRECIPITATION) == 0)
    bft_error(__FILE__, __LINE__, 0,
              " %s: Precipitation model has not been activated for this"
              " tracer", __func__);

  cs_gwf_tracer_context_t  *tc = tracer->context;

  /* Look for the related soil */
  if (soil_name == NULL) { /* All soils have to be set for this tracer */

    const int n_soils = cs_gwf_get_n_soils();
    for (int soil_id = 0; soil_id < n_soils; soil_id++)
      tc->conc_w_star[soil_id] = conc_w_star;

  }
  else { /* Set this tracer equation for a specific soil */

    cs_gwf_soil_t  *soil = cs_gwf_soil_by_name(soil_name);
    if (soil == NULL)
      bft_error(__FILE__, __LINE__, 0,
                " Soil %s not found among the predefined soils.\n"
                " Please check your settings.", soil_name);

    tc->conc_w_star[soil->id] = conc_w_star;

  } /* Set a specific couple (tracer, soil) */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Add terms to the algebraic system related to a tracer equation
 *         according to the settings.
 *         Case of the default tracer modelling
 *         Rely on the generic function: cs_gwf_tracer_add_terms_t
 *
 * \param[in, out] tracer       pointer to a cs_gwf_tracer_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_gwf_tracer_add_terms(cs_gwf_tracer_t     *tracer)
{
  /* Sanity check */
  if (tracer == NULL)
    bft_error(__FILE__, __LINE__, 0,
              " At least one tracer equation has not been set.\n"
              " Please check your settings.");

  cs_gwf_tracer_context_t  *tc = tracer->context;
  cs_equation_param_t  *eqp = cs_equation_get_param(tracer->eq);

  const int n_soils = cs_gwf_get_n_soils();
  const double  thd = 100*DBL_MIN; /* threshold to avoid a wrong activation */
  const char *eq_name = cs_equation_get_name(tracer->eq);

  bool  do_diffusion = false, do_reaction = false;

  /* Loop on soils to check in a reaction term is needed */
  for (int soil_id = 0; soil_id < n_soils; soil_id++) {

    if (fabs(tc->alpha_t[soil_id]) > thd) do_diffusion = true;
    if (fabs(tc->alpha_l[soil_id]) > thd) do_diffusion = true;
    if (tc->wmd[soil_id] > thd) do_diffusion = true;
    if (fabs(tc->reaction_rate[soil_id]) > thd) do_reaction = true;

  }

  int  max_len = 0;
  char  *name = NULL;

  const int  log_key = cs_field_key_id("log");
  const int  c_loc_id = cs_mesh_location_get_id_by_name("cells");
  const int  post_key = cs_field_key_id("post_vis");

  if (do_diffusion) { /* Add a new diffusion property for this equation */

    int  len = strlen(eq_name) + strlen("_diffusivity") + 1;
    if (len > max_len) {
      max_len = len;
      BFT_REALLOC(name, len, char);
    }
    sprintf(name, "%s_diffusivity", eq_name);

    cs_property_t *diff_pty = cs_property_add(name, CS_PROPERTY_ANISO);

    cs_equation_add_diffusion(eqp, diff_pty);

    /* Create a new field related to this property */
    const int  pty_mask = CS_FIELD_INTENSIVE | CS_FIELD_PROPERTY;
    const bool  pty_has_previous = false; /* no previous snapshot */
    const int  field_dim = 9;             /* anisotropic */

    tracer->diffusivity = cs_field_create(name,
                                          pty_mask,
                                          c_loc_id,
                                          field_dim,
                                          pty_has_previous);

    cs_field_set_key_int(tracer->diffusivity, cs_field_key_id("log"), 1);

  } /* diffusion */

  if (do_reaction) { /* Add a new reaction property for this equation */

    int  len = strlen(eq_name) + strlen("_reaction") + 1;
    if (len > max_len) {
      max_len = len;
      BFT_REALLOC(name, len, char);
    }
    sprintf(name, "%s_reaction", eq_name);

    cs_property_t *r_pty = cs_property_add(name, CS_PROPERTY_ISO);

    tracer->reaction_id = cs_equation_add_reaction(eqp, r_pty);

  } /* reaction */

  if (tracer->model & CS_GWF_TRACER_PRECIPITATION) {

    bool has_previous = false;  /* Not useful up to now */
    int  len = strlen(eq_name) + strlen("_precip") + 1;
    if (len > max_len) {
      max_len = len;
      BFT_REALLOC(name, len, char);
    }
    sprintf(name, "%s_precip", eq_name);

    tc->precip_field = cs_field_create(name,
                                       CS_FIELD_INTENSIVE | CS_FIELD_CDO,
                                       c_loc_id,
                                       1,
                                       has_previous);

    cs_field_set_key_int(tc->precip_field, log_key, 1);
    cs_field_set_key_int(tc->precip_field, post_key, 1);

  }

  BFT_FREE(name);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the parameters related to a standard tracer equation
 *
 * \param[in]      connect       pointer to a cs_cdo_connect_t structure
 * \param[in]      quant         pointer to a cs_cdo_quantities_t structure
 * \param[in]      adv           pointer to an advection field structure
 * \param[in]      l_saturation  pointer to the liquid saturation values
 * \param[in, out] tracer        pointer to a cs_gwf_tracer_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_gwf_tracer_saturated_setup(const cs_cdo_connect_t      *connect,
                              const cs_cdo_quantities_t   *quant,
                              const cs_adv_field_t        *adv,
                              const cs_real_t             *l_saturation,
                              cs_gwf_tracer_t             *tracer)
{
  /* Sanity check */

  if (tracer == NULL)
    bft_error(__FILE__, __LINE__, 0,
              " At least one tracer equation has not been set.\n"
              " Please check your settings.");

  const int  n_soils = cs_gwf_get_n_soils();
  const cs_flag_t  eq_flag = cs_equation_get_flag(tracer->eq);

  cs_gwf_tracer_context_t  *tc = tracer->context;

  /* Set additional (pre-defined) fields */

  tc->darcy_velocity_field =
    cs_advection_field_get_field(adv, CS_MESH_LOCATION_CELLS);
  tc->l_saturation = l_saturation;

  /* We assume that the unsteady term is always activated */

  cs_property_t  *pty = cs_equation_get_time_property(tracer->eq);
  assert(pty != NULL);

  for (int soil_id = 0; soil_id < n_soils; soil_id++) {

    const cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);
    const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);

    cs_property_def_by_func(pty,
                            z->name,
                            (void *)tracer->context,
                            _get_time_pty4std_sat_tracer,
                            _get_time_pty4std_sat_tracer_cw);

  } /* Loop on soils */

  if (eq_flag & CS_EQUATION_DIFFUSION) { /* Setup the diffusion property */

    tracer->update_diff_tensor = _update_sat_diff_pty;

    assert(tracer->diffusivity != NULL &&
           tracer->diffusivity->val != NULL); /* Should be done previously */

    cs_property_t  *diff_pty = cs_equation_get_diffusion_property(tracer->eq);

    cs_property_def_by_field(diff_pty, tracer->diffusivity);

  } /* diffusion */

  if (eq_flag & CS_EQUATION_REACTION) { /* Setup the reaction property */

    for (int soil_id = 0; soil_id < n_soils; soil_id++) {

      const cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);
      const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);

      cs_property_t  *r_pty =
        cs_equation_get_reaction_property(tracer->eq, tracer->reaction_id);

      if (r_pty != NULL) /* The default reaction property is defined */
        cs_property_def_by_func(r_pty,
                                z->name,
                                (void *)tracer->context,
                                _get_reaction_pty4std_sat_tracer,
                                _get_reaction_pty4std_sat_tracer_cw);

    } /* Loop on soils */

  } /* reaction */

  /* Precipitation modelling */

  if (tracer->model & CS_GWF_TRACER_PRECIPITATION)
    _add_precipitation(connect, quant, tracer);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the parameters related to a standard tracer equation in case of
 *         an unsaturated flow model
 *
 * \param[in]      connect       pointer to a cs_cdo_connect_t structure
 * \param[in]      quant         pointer to a cs_cdo_quantities_t structure
 * \param[in]      adv           pointer to an advection field structure
 * \param[in]      l_saturation  pointer to the liquid saturation values
 * \param[in, out] tracer        pointer to a cs_gwf_tracer_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_gwf_tracer_unsaturated_setup(const cs_cdo_connect_t      *connect,
                                const cs_cdo_quantities_t   *quant,
                                const cs_adv_field_t        *adv,
                                const cs_real_t             *l_saturation,
                                cs_gwf_tracer_t             *tracer)
{
  /* Sanity check */

  if (tracer == NULL)
    bft_error(__FILE__, __LINE__, 0,
              " At least one tracer equation has not been set.\n"
              " Please check your settings.");

  const int  n_soils = cs_gwf_get_n_soils();
  const cs_flag_t  eq_flag = cs_equation_get_flag(tracer->eq);

  cs_gwf_tracer_context_t  *tc = tracer->context;

  /* Set additional (pre-defined) fields */

  tc->darcy_velocity_field =
    cs_advection_field_get_field(adv, CS_MESH_LOCATION_CELLS);
  tc->l_saturation = l_saturation;

  /* We assume that the unsteady term is always activated */

  cs_property_t  *pty = cs_equation_get_time_property(tracer->eq);
  assert(pty != NULL);

  for (int soil_id = 0; soil_id < n_soils; soil_id++) {

    const cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);
    const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);

    cs_property_def_by_func(pty,
                            z->name,
                            (void *)tracer->context,
                            _get_time_pty4std_tracer,
                            _get_time_pty4std_tracer_cw);

  } /* Loop on soils */

  if (eq_flag & CS_EQUATION_DIFFUSION) { /* Setup the diffusion property */

    assert(tracer->diffusivity != NULL &&
           tracer->diffusivity->val != NULL); /* Should be done previously */

    cs_property_t  *diff_pty = cs_equation_get_diffusion_property(tracer->eq);

    cs_property_def_by_field(diff_pty, tracer->diffusivity);

  } /* diffusion */

  if (eq_flag & CS_EQUATION_REACTION) { /* Setup the reaction property */

    for (int soil_id = 0; soil_id < n_soils; soil_id++) {

      const cs_gwf_soil_t  *soil = cs_gwf_soil_by_id(soil_id);
      const cs_zone_t  *z = cs_volume_zone_by_id(soil->zone_id);

      cs_property_t  *r_pty =
        cs_equation_get_reaction_property(tracer->eq, tracer->reaction_id);

      if (r_pty != NULL) /* The default reaction property is defined */
        cs_property_def_by_func(r_pty,
                                z->name,
                                (void *)tracer->context,
                                _get_reaction_pty4std_tracer,
                                _get_reaction_pty4std_tracer_cw);

    } /* Loop on soils */

  } /* reaction */

  /* Precipitation modelling */

  if (tracer->model & CS_GWF_TRACER_PRECIPITATION)
    _add_precipitation(connect, quant, tracer);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Display the main features related to a tracer
 *
 * \param[in]  tracer   pointer to a cs_gwf_tracer_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_gwf_tracer_log_setup(const cs_gwf_tracer_t     *tracer)
{
  if (tracer == NULL)
    return;

  cs_field_t  *f = cs_equation_get_field(tracer->eq);

  cs_log_printf(CS_LOG_SETUP,
                "  * GWF | Tracer: %s (variable: %s)\n",
                cs_equation_get_name(tracer->eq), f->name);
  if (tracer->model & CS_GWF_TRACER_USER)
    cs_log_printf(CS_LOG_SETUP,
                  "  * GWF | Tracer: User-defined model\n");

  else {

    cs_log_printf(CS_LOG_SETUP, "  * GWF | Tracer: Default model\n");
    if (tracer->model & CS_GWF_TRACER_PRECIPITATION)
      cs_log_printf(CS_LOG_SETUP, "  * GWF | + Precipitation effects\n");
    if (tracer->model & CS_GWF_TRACER_SORPTION_EK_3_PARAMETERS)
      cs_log_printf(CS_LOG_SETUP, "  * GWF | + EK model with 3 parameters\n");
    else if (tracer->model & CS_GWF_TRACER_SORPTION_EK_5_PARAMETERS)
      cs_log_printf(CS_LOG_SETUP, "  * GWF | + EK model with 5 parameters\n");

  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the integral over a given set of cells of the field related
 *         to a tracer equation. This integral turns out to be exact for linear
 *         functions.
 *
 * \param[in]    connect   pointer to a \ref cs_cdo_connect_t structure
 * \param[in]    cdoq      pointer to a \ref cs_cdo_quantities_t structure
 * \param[in]    tracer    pointer to a \ref cs_gwf_tracer_t structure
 * \param[in]    z         pointer to a volume zone structure
 *
 * \return the value of the integral
 */
/*----------------------------------------------------------------------------*/

cs_real_t
cs_gwf_tracer_integrate(const cs_cdo_connect_t     *connect,
                        const cs_cdo_quantities_t  *cdoq,
                        const cs_gwf_tracer_t      *tracer,
                        const cs_zone_t            *z)
{
  assert(tracer != NULL);

  cs_gwf_tracer_context_t  *tc = tracer->context;

  const cs_equation_param_t  *tr_eqp = cs_equation_get_param(tracer->eq);
  const short int  *c2s = cs_gwf_get_cell2soil();
  const cs_real_t  *moisture_val = tc->l_saturation;

  if (moisture_val == NULL)
    bft_error(__FILE__, __LINE__, 0, " %s: \"moisture_content\" not defined",
              __func__);

  cs_real_t  int_value = 0.0;

  switch (tr_eqp->space_scheme) {

  case CS_SPACE_SCHEME_CDOVB:
    {
      const cs_real_t  *v_vals = cs_equation_get_vertex_values(tracer->eq,
                                                               false);
      const cs_adjacency_t  *c2v = connect->c2v;

      for (cs_lnum_t i = 0; i < z->n_elts; i++) {

        const cs_lnum_t  c_id = (z->elt_ids == NULL) ? i : z->elt_ids[i];

        cs_real_t  _int_value = 0.;
        for (cs_lnum_t j = c2v->idx[c_id]; j < c2v->idx[c_id+1]; j++) {
          _int_value += cdoq->dcell_vol[j] * v_vals[c2v->ids[j]];
        }

        int_value +=
          (moisture_val[c_id] + tc->rho_kd[c2s[c_id]]) * _int_value;

      } /* Loop on selected cells */

    }
    break; /* CS_SPACE_SCHEME_CDOVB */

  case CS_SPACE_SCHEME_CDOVCB:
    {
      const cs_real_t  *v_vals = cs_equation_get_vertex_values(tracer->eq,
                                                               false);
      const cs_real_t  *c_vals = cs_equation_get_cell_values(tracer->eq,
                                                             false);
      const cs_adjacency_t  *c2v = connect->c2v;

      for (cs_lnum_t i = 0; i < z->n_elts; i++) {

        const cs_lnum_t  c_id = (z->elt_ids == NULL) ? i : z->elt_ids[i];

        /* Shares between cell and vertex unknowns:
           - the cell unknown stands for 1/4 of the cell volume
           - the vertex unknown stands for 3/4 of the dual cell volume
        */

        cs_real_t  _int_value = 0.25*cdoq->cell_vol[c_id]*c_vals[c_id];
        for (cs_lnum_t j = c2v->idx[c_id]; j < c2v->idx[c_id+1]; j++)
          _int_value += 0.75 * cdoq->dcell_vol[j] * v_vals[c2v->ids[j]];

        int_value +=
          (moisture_val[c_id] + tc->rho_kd[c2s[c_id]]) * _int_value;

      } /* Loop on selected cells */

    }
    break; /* CS_SPACE_SCHEME_CDOVCB */

  default:
    bft_error(__FILE__, __LINE__, 0, " %s: Invalid space scheme", __func__);
    break;

  } /* End of switch */

  /* Parallel synchronization */

  if (cs_glob_n_ranks > 1)
    cs_parall_sum(1, CS_REAL_TYPE, &int_value);

  return int_value;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the integral over a given set of cells of the field related
 *         to a tracer equation. This integral turns out to be exact for linear
 *         functions.
 *         Case of a fully saturated model.
 *
 * \param[in]    connect   pointer to a \ref cs_cdo_connect_t structure
 * \param[in]    cdoq      pointer to a \ref cs_cdo_quantities_t structure
 * \param[in]    tracer    pointer to a \ref cs_gwf_tracer_t structure
 * \param[in]    z         pointer to a volume zone structure
 *
 * \return the value of the integral
 */
/*----------------------------------------------------------------------------*/

cs_real_t
cs_gwf_tracer_integrate_sat(const cs_cdo_connect_t     *connect,
                            const cs_cdo_quantities_t  *cdoq,
                            const cs_gwf_tracer_t      *tracer,
                            const cs_zone_t            *z)
{
  assert(tracer != NULL);

  cs_gwf_tracer_context_t  *tc = tracer->context;

  const cs_equation_param_t  *tr_eqp = cs_equation_get_param(tracer->eq);
  const short int  *c2s = cs_gwf_get_cell2soil();

  cs_real_t  int_value = 0.0;

  switch (tr_eqp->space_scheme) {

  case CS_SPACE_SCHEME_CDOVB:
    {
      const cs_real_t  *v_vals = cs_equation_get_vertex_values(tracer->eq,
                                                               false);
      const cs_adjacency_t  *c2v = connect->c2v;

      for (cs_lnum_t i = 0; i < z->n_elts; i++) {

        const cs_lnum_t  c_id = (z->elt_ids == NULL) ? i : z->elt_ids[i];
        const short int  s = c2s[c_id];
        const cs_real_t  sat_moisture = cs_gwf_soil_get_saturated_moisture(s);

        cs_real_t  _int_value = 0.;
        for (cs_lnum_t j = c2v->idx[c_id]; j < c2v->idx[c_id+1]; j++) {
          _int_value += cdoq->dcell_vol[j] * v_vals[c2v->ids[j]];
        }

        int_value += (sat_moisture + tc->rho_kd[s]) * _int_value;

      } /* Loop on selected cells */

    }
    break; /* CS_SPACE_SCHEME_CDOVB */

  case CS_SPACE_SCHEME_CDOVCB:
    {
      const cs_real_t  *v_vals = cs_equation_get_vertex_values(tracer->eq,
                                                               false);
      const cs_real_t  *c_vals = cs_equation_get_cell_values(tracer->eq,
                                                             false);
      const cs_adjacency_t  *c2v = connect->c2v;

      for (cs_lnum_t i = 0; i < z->n_elts; i++) {

        const cs_lnum_t  c_id = (z->elt_ids == NULL) ? i : z->elt_ids[i];
        const short int  s = c2s[c_id];
        const cs_real_t  sat_moisture = cs_gwf_soil_get_saturated_moisture(s);

        /* Shares between cell and vertex unknowns:
           - the cell unknown stands for 1/4 of the cell volume
           - the vertex unknown stands for 3/4 of the dual cell volume
        */

        cs_real_t  _int_value = 0.25*cdoq->cell_vol[c_id]*c_vals[c_id];
        for (cs_lnum_t j = c2v->idx[c_id]; j < c2v->idx[c_id+1]; j++)
          _int_value += 0.75 * cdoq->dcell_vol[j] * v_vals[c2v->ids[j]];

        int_value += (sat_moisture + tc->rho_kd[s]) * _int_value;

      } /* Loop on selected cells */

    }
    break; /* CS_SPACE_SCHEME_CDOVCB */

  default:
    bft_error(__FILE__, __LINE__, 0, " %s: Invalid space scheme", __func__);
    break;

  } /* End of switch */

  /* Parallel synchronization */

  if (cs_glob_n_ranks > 1)
    cs_parall_sum(1, CS_REAL_TYPE, &int_value);

  return int_value;
}

/*----------------------------------------------------------------------------*/

END_C_DECLS
