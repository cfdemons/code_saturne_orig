#ifndef __CS_CDOFB_NAVSTO_H__
#define __CS_CDOFB_NAVSTO_H__

/*============================================================================
 * Functions shared among all face-based schemes for the discretization of the
 * Navier--Stokes system
 *============================================================================*/

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

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "cs_base.h"
#include "cs_cdo_connect.h"
#include "cs_cdo_quantities.h"
#include "cs_field.h"
#include "cs_math.h"
#include "cs_matrix.h"
#include "cs_mesh.h"
#include "cs_navsto_param.h"
#include "cs_time_step.h"
#include "cs_sdm.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Macro definitions
 *============================================================================*/

/*============================================================================
 * Type definitions
 *============================================================================*/

/* Structure storing additional arrays related to the building of the system in
   case of CDO Face-based scheme. This structure is associated to a cell-wise
   building */

typedef struct {

  /* Value of the mass density for the current cell */
  cs_real_t            rho_c;

  /* Operator */
  cs_real_t           *div_op;           /* Size: 3*n_fc
                                            div_op = -|c|div */

  /* Boundary conditions */
  cs_boundary_type_t  *bf_type;          /* Size: n_fc */
  cs_real_t           *pressure_bc_val;  /* Size: n_fc */

} cs_cdofb_navsto_builder_t;

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute and add a source term to the local RHS.
 *         This is a special treatment to enable source involving face DoFs and
 *         potentially the local discrete divergence/gradient operators.
 *         In the standard case, only the cell DoFs are involved.
 *         Examples are gravity term or Bousinesq term(s)
 *
 * \param[in]      nsp     set of parameters to handle the Navier-Stokes system
 * \param[in]      cm      pointer to a cs_cell_mesh_t structure
 * \param[in]      nsb     pointer to a builder structure for the NavSto system
 * \param[in, out] csys    pointer to a cs_cell_sys_t structure
 */
/*----------------------------------------------------------------------------*/

typedef void
(cs_cdofb_navsto_source_t)(const cs_navsto_param_t           *nsp,
                           const cs_cell_mesh_t              *cm,
                           const cs_cdofb_navsto_builder_t   *nsb,
                           cs_cell_sys_t                     *csys);

/*============================================================================
 * Static inline public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the divergence vector associated to the current cell.
 *         WARNING: mind that, differently form the original definition, the
 *         result here is not divided by the cell volume
 *
 * \param[in]      cm         pointer to a \ref cs_cell_mesh_t structure
 * \param[in, out] div        array related to the divergence operator
 */
/*----------------------------------------------------------------------------*/

static inline void
cs_cdofb_navsto_divergence_vect(const cs_cell_mesh_t  *cm,
                                cs_real_t              div[])
{
  /* D(\hat{u}) = \frac{1}{|c|} \sum_{f_c} \iota_{fc} u_f.f
   * But, when integrating [[ p, q ]]_{P_c} = |c| p_c q_c
   * Thus, the volume in the divergence drops
   */

  for (short int f = 0; f < cm->n_fc; f++) {

    const cs_quant_t  pfq = cm->face[f];
    const cs_real_t  i_f = cm->f_sgn[f] * pfq.meas;

    cs_real_t  *_div_f = div + 3*f;
    _div_f[0] = i_f * pfq.unitv[0];
    _div_f[1] = i_f * pfq.unitv[1];
    _div_f[2] = i_f * pfq.unitv[2];

  } /* Loop on cell faces */
}

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Create and allocate a local NavSto builder when Fb schemes are used
 *
 * \param[in] nsp         set of parameters to define the NavSto system
 * \param[in] connect     pointer to a cs_cdo_connect_t structure
 *
 * \return a cs_cdofb_navsto_builder_t structure
 */
/*----------------------------------------------------------------------------*/

cs_cdofb_navsto_builder_t
cs_cdofb_navsto_create_builder(const cs_navsto_param_t  *nsp,
                               const cs_cdo_connect_t   *connect);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Destroy the given cs_cdofb_navsto_builder_t structure
 *
 * \param[in, out] nsb   pointer to the cs_cdofb_navsto_builder_t to free
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_free_builder(cs_cdofb_navsto_builder_t   *nsb);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the members of the cs_cdofb_navsto_builder_t structure
 *
 * \param[in]      t_eval     time at which one evaluates the pressure BC
 * \param[in]      nsp        set of parameters to define the NavSto system
 * \param[in]      cm         cellwise view of the mesh
 * \param[in]      csys       cellwise view of the algebraic system
 * \param[in]      pr_bc      set of definitions for the presuure BCs
 * \param[in]      bf_type    type of boundaries for all boundary faces
 * \param[in, out] nsb        builder to update
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_define_builder(cs_real_t                    t_eval,
                               const cs_navsto_param_t     *nsp,
                               const cs_cell_mesh_t        *cm,
                               const cs_cell_sys_t         *csys,
                               const cs_cdo_bc_face_t      *pr_bc,
                               const cs_boundary_type_t    *bf_type,
                               cs_cdofb_navsto_builder_t   *nsb);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free allocated structures associated to this file
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_finalize(void);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the mass flux playing the role of the advection field in
 *         the Navier-Stokes equations
 *         One considers the mass flux across primal faces which relies on the
 *         velocity vector defined on each face.
 *
 * \param[in]      nsp         set of parameters to define the NavSto system
 * \param[in]      quant       set of additional geometrical quantities
 * \param[in]      face_vel    velocity vectors for each face
 * \param[in, out] mass_flux   array of mass flux values to update (allocated)
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_mass_flux(const cs_navsto_param_t     *nsp,
                          const cs_cdo_quantities_t   *quant,
                          const cs_real_t             *face_vel,
                          cs_real_t                   *mass_flux);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the divergence of a cell using the \ref cs_cdo_quantities_t
 *         structure
 *
 * \param[in]     c_id         cell id
 * \param[in]     quant        pointer to a \ref cs_cdo_quantities_t
 * \param[in]     c2f          pointer to cell-to-face \ref cs_adjacency_t
 * \param[in]     f_vals       values of the face DoFs
 *
 * \return the divergence for the corresponding cell
 */
/*----------------------------------------------------------------------------*/

cs_real_t
cs_cdofb_navsto_cell_divergence(const cs_lnum_t               c_id,
                                const cs_cdo_quantities_t    *quant,
                                const cs_adjacency_t         *c2f,
                                const cs_real_t              *f_vals);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Add the grad-div part to the local matrix (i.e. for the current
 *         cell)
 *
 * \param[in]      n_fc       local number of faces for the current cell
 * \param[in]      zeta       scalar coefficient for the grad-div operator
 * \param[in]      div        divergence
 * \param[in, out] mat        local system matrix to update
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_add_grad_div(short int          n_fc,
                             const cs_real_t    zeta,
                             const cs_real_t    div[],
                             cs_sdm_t          *mat);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize the pressure values
 *
 * \param[in]       nsp     pointer to a \ref cs_navsto_param_t structure
 * \param[in]       quant   pointer to a \ref cs_cdo_quantities_t structure
 * \param[in]       ts      pointer to a \ref cs_time_step_t structure
 * \param[in, out]  pr      pointer to the pressure \ref cs_field_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_init_pressure(const cs_navsto_param_t     *nsp,
                              const cs_cdo_quantities_t   *quant,
                              const cs_time_step_t        *ts,
                              cs_field_t                  *pr);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize the pressure values when the pressure is defined at
 *         faces
 *
 * \param[in]       nsp       pointer to a \ref cs_navsto_param_t structure
 * \param[in]       connect   pointer to a \ref cs_cdo_connect_t structure
 * \param[in]       ts        pointer to a \ref cs_time_step_t structure
 * \param[in, out]  pr_f      pointer to the pressure values at faces
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_init_face_pressure(const cs_navsto_param_t     *nsp,
                                   const cs_cdo_connect_t      *connect,
                                   const cs_time_step_t        *ts,
                                   cs_real_t                   *pr_f);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Update the pressure field in order to get a field with a mean-value
 *         equal to the reference value
 *
 * \param[in]       nsp       pointer to a cs_navsto_param_t structure
 * \param[in]       quant     pointer to a cs_cdo_quantities_t structure
 * \param[in, out]  values    pressure field values
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_rescale_pressure_to_ref(const cs_navsto_param_t    *nsp,
                                        const cs_cdo_quantities_t  *quant,
                                        cs_real_t                   values[]);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Update the pressure field in order to get a field with a zero-mean
 *         average
 *
 * \param[in]       quant     pointer to a cs_cdo_quantities_t structure
 * \param[in, out]  values    pressure field values
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_set_zero_mean_pressure(const cs_cdo_quantities_t  *quant,
                                       cs_real_t                   values[]);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Perform extra-operation related to Fb schemes when solving
 *         Navier-Stokes.
 *         - Compute the mass flux accross the boundaries.
 *         - Compute the kinetic energy
 *         - Compute the velocity gradient
 *         - Compute the vorticity
 *         - Compute the helicity
 *         - Compute the enstrophy
 *         - Compute the stream function
 *
 * \param[in]  nsp        pointer to a \ref cs_navsto_param_t struct.
 * \param[in]  mesh       pointer to a cs_mesh_t structure
 * \param[in]  quant      pointer to a \ref cs_cdo_quantities_t struct.
 * \param[in]  connect    pointer to a \ref cs_cdo_connect_t struct.
 * \param[in]  ts         pointer to a \ref cs_time_step_t struct.
 * \param[in]  adv_field  pointer to a \ref cs_adv_field_t struct.
 * \param[in]  mass_flux  scalar-valued mass flux for each face
 * \param[in]  u_cell     vector-valued velocity in each cell
 * \param[in]  u_face     vector-valued velocity on each face
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_extra_op(const cs_navsto_param_t     *nsp,
                         const cs_mesh_t             *mesh,
                         const cs_cdo_quantities_t   *quant,
                         const cs_cdo_connect_t      *connect,
                         const cs_time_step_t        *ts,
                         const cs_adv_field_t        *adv_field,
                         const cs_real_t             *mass_flux,
                         const cs_real_t             *u_cell,
                         const cs_real_t             *u_face);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Take into account a Dirichlet BCs on the three velocity components.
 *         For instance for a velocity inlet boundary or a wall
 *         Handle the velocity-block in the global algebraic system in case of
 *         an algebraic technique.
 *         This prototype matches the function pointer cs_cdo_apply_boundary_t
 *
 * \param[in]       f         face id in the cell mesh numbering
 * \param[in]       eqp       pointer to a \ref cs_equation_param_t struct.
 * \param[in]       cm        pointer to a \ref cs_cell_mesh_t structure
 * \param[in]       pty       pointer to a \ref cs_property_data_t structure
 * \param[in, out]  cb        pointer to a \ref cs_cell_builder_t structure
 * \param[in, out]  csys      structure storing the cellwise system
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_block_dirichlet_alge(short int                       f,
                              const cs_equation_param_t      *eqp,
                              const cs_cell_mesh_t           *cm,
                              const cs_property_data_t       *pty,
                              cs_cell_builder_t              *cb,
                              cs_cell_sys_t                  *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Take into account a Dirichlet BCs on the three velocity components.
 *         For instance for a velocity inlet boundary or a wall
 *         Handle the velocity-block in the global algebraic system in case of
 *         a penalization technique (with a large coefficient).
 *         One assumes that static condensation has been performed and that
 *         the velocity-block has size 3*n_fc
 *         This prototype matches the function pointer cs_cdo_apply_boundary_t
 *
 * \param[in]       f         face id in the cell mesh numbering
 * \param[in]       eqp       pointer to a \ref cs_equation_param_t struct.
 * \param[in]       cm        pointer to a \ref cs_cell_mesh_t structure
 * \param[in]       pty       pointer to a \ref cs_property_data_t structure
 * \param[in, out]  cb        pointer to a \ref cs_cell_builder_t structure
 * \param[in, out]  csys      structure storing the cellwise system
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_block_dirichlet_pena(short int                       f,
                              const cs_equation_param_t      *eqp,
                              const cs_cell_mesh_t           *cm,
                              const cs_property_data_t       *pty,
                              cs_cell_builder_t              *cb,
                              cs_cell_sys_t                  *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Take into account a Dirichlet BCs on the three velocity components.
 *         For instance for a velocity inlet boundary or a wall
 *         Handle the velocity-block in the global algebraic system in case of
 *         a weak penalization technique (Nitsche).
 *         One assumes that static condensation has not been performed yet and
 *         that the velocity-block has size 3*(n_fc + 1)
 *         This prototype matches the function pointer cs_cdo_apply_boundary_t
 *
 * \param[in]       fb        face id in the cell mesh numbering
 * \param[in]       eqp       pointer to a \ref cs_equation_param_t struct.
 * \param[in]       cm        pointer to a \ref cs_cell_mesh_t structure
 * \param[in]       pty       pointer to a \ref cs_property_data_t structure
 * \param[in, out]  cb        pointer to a \ref cs_cell_builder_t structure
 * \param[in, out]  csys      structure storing the cellwise system
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_block_dirichlet_weak(short int                       fb,
                              const cs_equation_param_t      *eqp,
                              const cs_cell_mesh_t           *cm,
                              const cs_property_data_t       *pty,
                              cs_cell_builder_t              *cb,
                              cs_cell_sys_t                  *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Take into account a Dirichlet BCs on the three velocity components.
 *         For instance for a velocity inlet boundary or a wall
 *         Handle the velocity-block in the global algebraic system in case of
 *         a weak penalization technique (symmetrized Nitsche).
 *         One assumes that static condensation has not been performed yet and
 *         that the velocity-block has size 3*(n_fc + 1)
 *         This prototype matches the function pointer cs_cdo_apply_boundary_t
 *
 * \param[in]       fb        face id in the cell mesh numbering
 * \param[in]       eqp       pointer to a \ref cs_equation_param_t struct.
 * \param[in]       cm        pointer to a \ref cs_cell_mesh_t structure
 * \param[in]       pty       pointer to a \ref cs_property_data_t structure
 * \param[in, out]  cb        pointer to a \ref cs_cell_builder_t structure
 * \param[in, out]  csys      structure storing the cellwise system
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_block_dirichlet_wsym(short int                       fb,
                              const cs_equation_param_t      *eqp,
                              const cs_cell_mesh_t           *cm,
                              const cs_property_data_t       *pty,
                              cs_cell_builder_t              *cb,
                              cs_cell_sys_t                  *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Take into account a boundary defined as 'symmetry' (treated as a
 *         sliding BCs on the three velocity components.)
 *         A weak penalization technique (symmetrized Nitsche) is used.  One
 *         assumes that static condensation has not been performed yet and that
 *         the velocity-block has (n_fc + 1) blocks of size 3x3.
 *         This prototype matches the function pointer cs_cdo_apply_boundary_t
 *
 * \param[in]       fb        face id in the cell mesh numbering
 * \param[in]       eqp       pointer to a \ref cs_equation_param_t struct.
 * \param[in]       cm        pointer to a \ref cs_cell_mesh_t structure
 * \param[in]       pty       pointer to a \ref cs_property_data_t structure
 * \param[in, out]  cb        pointer to a \ref cs_cell_builder_t structure
 * \param[in, out]  csys      structure storing the cellwise system
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_symmetry(short int                       fb,
                  const cs_equation_param_t      *eqp,
                  const cs_cell_mesh_t           *cm,
                  const cs_property_data_t       *pty,
                  cs_cell_builder_t              *cb,
                  cs_cell_sys_t                  *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief   Take into account a wall BCs by a weak enforcement using Nitsche
 *          technique plus a symmetric treatment.
 *          This prototype matches the function pointer cs_cdo_apply_boundary_t
 *
 * \param[in]       fb        face id in the cell mesh numbering
 * \param[in]       eqp       pointer to a \ref cs_equation_param_t struct.
 * \param[in]       cm        pointer to a \ref cs_cell_mesh_t structure
 * \param[in]       pty       pointer to a \ref cs_property_data_t structure
 * \param[in, out]  cb        pointer to a \ref cs_cell_builder_t structure
 * \param[in, out]  csys      structure storing the cellwise system
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_fixed_wall(short int                       fb,
                    const cs_equation_param_t      *eqp,
                    const cs_cell_mesh_t           *cm,
                    const cs_property_data_t       *pty,
                    cs_cell_builder_t              *cb,
                    cs_cell_sys_t                  *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the function pointer computing the source term in the momentum
 *         equation related to the gravity effect (hydrostatic pressure or the
 *         Boussinesq approximation)
 *
 * \param[in]  nsp          set of parameters for the Navier-Stokes system
 * \param[out] p_func       way to compute the gravity effect
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_set_gravity_func(const cs_navsto_param_t      *nsp,
                                 cs_cdofb_navsto_source_t    **p_func);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Take into account the gravity effects.
 *         Compute and add the source term to the local RHS.
 *         This is a special treatment since of face DoFs are involved
 *         contrary to the standard case where only the cell DoFs is involved.
 *
 * \param[in]      nsp     set of parameters to handle the Navier-Stokes system
 * \param[in]      cm      pointer to a cs_cell_mesh_t structure
 * \param[in]      nsb     pointer to a builder structure for the NavSto system
 * \param[in, out] csys    pointer to a cs_cell_sys_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_gravity_term(const cs_navsto_param_t           *nsp,
                             const cs_cell_mesh_t              *cm,
                             const cs_cdofb_navsto_builder_t   *nsb,
                             cs_cell_sys_t                     *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Take into account the buoyancy force with the Boussinesq approx.
 *         Compute and add the source term to the local RHS.
 *         This is the standard case where only the cell DoFs are involved.
 *
 * \param[in]      nsp     set of parameters to handle the Navier-Stokes system
 * \param[in]      cm      pointer to a cs_cell_mesh_t structure
 * \param[in]      nsb     pointer to a builder structure for the NavSto system
 * \param[in, out] csys    pointer to a cs_cell_sys_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_boussinesq_term(const cs_navsto_param_t           *nsp,
                                const cs_cell_mesh_t              *cm,
                                const cs_cdofb_navsto_builder_t   *nsb,
                                cs_cell_sys_t                     *csys);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get the source term for computing the stream function.
 *         This relies on the prototype associated to the generic function
 *         pointer \ref cs_dof_function_t
 *
 * \param[in]      n_elts        number of elements to consider
 * \param[in]      elt_ids       list of elements ids
 * \param[in]      dense_output  perform an indirection in retval or not
 * \param[in]      input         NULL or pointer to a structure cast on-the-fly
 * \param[in, out] retval        result of the function. Must be allocated.
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_stream_source_term(cs_lnum_t            n_elts,
                                   const cs_lnum_t     *elt_ids,
                                   bool                 dense_output,
                                   void                *input,
                                   cs_real_t           *retval);

/*----------------------------------------------------------------------------*/

END_C_DECLS

#endif /* __CS_CDOFB_NAVSTO_H__ */
