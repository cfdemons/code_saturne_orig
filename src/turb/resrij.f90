!-------------------------------------------------------------------------------

! This file is part of Code_Saturne, a general-purpose CFD tool.
!
! Copyright (C) 1998-2021 EDF S.A.
!
! This program is free software; you can redistribute it and/or modify it under
! the terms of the GNU General Public License as published by the Free Software
! Foundation; either version 2 of the License, or (at your option) any later
! version.
!
! This program is distributed in the hope that it will be useful, but WITHOUT
! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
! FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
! details.
!
! You should have received a copy of the GNU General Public License along with
! this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
! Street, Fifth Floor, Boston, MA 02110-1301, USA.

!-------------------------------------------------------------------------------

!===============================================================================
! Function:
! ---------

!> \file resrij.f90
!>
!> \brief This subroutine performs the solving of the Reynolds stress components
!> in \f$ R_{ij} - \varepsilon \f$ RANS (LRR) turbulence model.
!>
!> \remark
!> - isou=1 for \f$ R_{11} \f$
!> - isou=2 for \f$ R_{22} \f$
!> - isou=3 for \f$ R_{33} \f$
!> - isou=4 for \f$ R_{12} \f$
!> - isou=5 for \f$ R_{13} \f$
!> - isou=6 for \f$ R_{23} \f$
!-------------------------------------------------------------------------------

!-------------------------------------------------------------------------------
! Arguments
!______________________________________________________________________________.
!  mode           name          role
!______________________________________________________________________________!
!> \param[in]     nvar          total number of variables
!> \param[in]     nscal         total number of scalars
!> \param[in]     ncesmp        number of cells with mass source term
!> \param[in]     ivar          variable number
!> \param[in]     isou          local variable number (7 here)
!> \param[in]     icetsm        index of cells with mass source term
!> \param[in]     itypsm        type of mass source term for each variable
!>                               (see \ref cs_user_mass_source_terms)
!> \param[in]     dt            time step (per cell)
!> \param[in]     produc        work array for production
!> \param[in]     gradro        work array for grad rom
!>                              (without rho volume) only for iturb=30
!> \param[in]     smacel        value associated to each variable in the mass
!>                              source terms or mass rate (see
!>                              \ref cs_user_mass_source_terms)
!> \param[in]     viscf         visc*surface/dist at internal faces
!> \param[in]     viscb         visc*surface/dist at edge faces
!> \param[in]     tslagi        implicit source terms for the Lagrangian module
!> \param[in]     smbr          working array
!> \param[in]     rovsdt        working array
!______________________________________________________________________________!

subroutine resrij &
 ( nvar   , nscal  , ncesmp ,                                     &
   ivar   , isou   ,                                              &
   icetsm , itypsm ,                                              &
   dt     ,                                                       &
   produc , gradro ,                                              &
   smacel ,                                                       &
   viscf  , viscb  ,                                              &
   tslagi ,                                                       &
   smbr   , rovsdt )

!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use paramx
use numvar
use entsor
use optcal
use cstphy
use cstnum
use parall
use period
use lagran
use mesh
use field
use cs_f_interfaces
use rotation
use turbomachinery
use cs_c_bindings

!===============================================================================

implicit none

! Arguments

integer          nvar   , nscal
integer          ncesmp
integer          ivar   , isou

integer          icetsm(ncesmp), itypsm(ncesmp,nvar)

double precision dt(ncelet)
double precision produc(6,ncelet)
double precision gradro(3,ncelet)
double precision smacel(ncesmp,nvar)
double precision viscf(nfac), viscb(nfabor)
double precision tslagi(ncelet)
double precision smbr(ncelet), rovsdt(ncelet)

! Local variables

integer          iel, ifac
integer          ii    , jj    , kk    , comp_id, isou_ik, isou_jk
integer          iflmas, iflmab
integer          iwarnp
integer          imvisp
integer          iescap
integer          st_prv_id
integer          isoluc
integer          imucpp
integer          isou_r(3,3)
integer          icvflb
integer          init
integer          ivoid(1)
integer          key_t_ext_id
integer          iroext

double precision trprod, trrij , deltij
double precision tuexpr, thets , thetv , thetp1
double precision d1s3  , d2s3
double precision ccorio, matrot(3,3)
double precision rctse
double precision normp

double precision rvoid(1)

character(len=80) :: label
double precision, allocatable, dimension(:) :: w1
double precision, allocatable, dimension(:) :: w7, w8
double precision, allocatable, dimension(:) :: dpvar
double precision, allocatable, dimension(:,:) :: viscce
double precision, allocatable, dimension(:,:) :: weighf
double precision, allocatable, dimension(:) :: weighb
double precision, allocatable, dimension(:) :: cvar_var, cvara_var
double precision, allocatable, dimension(:) :: coefap, coefbp, cofafp, cofbfp
double precision, dimension(:), pointer :: imasfl, bmasfl
double precision, dimension(:), pointer :: crom, cromo
double precision, dimension(:,:), pointer :: coefap_rij, cofafp_rij
double precision, dimension(:,:,:), pointer:: coefbp_rij, cofbfp_rij
double precision, dimension(:,:), pointer :: visten, c_st_prv, lagr_st_rij
double precision, dimension(:), pointer :: cvara_ep
double precision, dimension(:,:), pointer :: cvara_rij, cvar_rij
double precision, dimension(:), pointer :: viscl

type(var_cal_opt) :: vcopt
type(var_cal_opt), target :: vcopt_loc
type(var_cal_opt), pointer :: p_k_value
type(c_ptr) :: c_k_value

character(len=len_trim(nomva0)+1, kind=c_char) :: c_name

!===============================================================================

!===============================================================================
! 1. Initialization
!===============================================================================

! Time extrapolation?
call field_get_key_id("time_extrapolated", key_t_ext_id)

call field_get_key_int(icrom, key_t_ext_id, iroext)

! Allocate work arrays
allocate(w1(ncelet))
allocate(w7(ncelet), w8(ncelet))
allocate(dpvar(ncelet))
allocate(viscce(6,ncelet))
allocate(weighf(2,nfac))
allocate(weighb(nfabor))

call field_get_key_struct_var_cal_opt(ivarfl(ivar), vcopt)

if (vcopt%iwarni.ge.1) then
  call field_get_label(ivarfl(ivar), label)
  write(nfecra,1000) label
endif

call field_get_val_s(icrom, crom)
call field_get_val_s(iviscl, viscl)
call field_get_key_int(ivarfl(iu), kimasf, iflmas)
call field_get_key_int(ivarfl(iu), kbmasf, iflmab)
call field_get_val_s(iflmas, imasfl)
call field_get_val_s(iflmab, bmasfl)

call field_get_val_prev_s(ivarfl(iep), cvara_ep)

call field_get_val_v(ivarfl(irij), cvar_rij)
call field_get_val_prev_v(ivarfl(irij), cvara_rij)

call field_get_coefa_v(ivarfl(irij), coefap_rij)
call field_get_coefb_v(ivarfl(irij), coefbp_rij)
call field_get_coefaf_v(ivarfl(irij), cofafp_rij)
call field_get_coefbf_v(ivarfl(irij), cofbfp_rij)

! Copy field components to scalar value

allocate(coefap(nfabor), coefbp(nfabor))
allocate(cofafp(nfabor), cofbfp(nfabor))
allocate(cvar_var(ncelet))
allocate(cvara_var(ncelet))

do iel = 1, ncel
  cvar_var(iel) = cvar_rij(isou,iel)
  cvara_var(iel) = cvara_rij(isou,iel)
enddo

do ifac = 1, nfabor
  coefap(ifac) = coefap_rij(isou,ifac)
  cofafp(ifac) = cofafp_rij(isou,ifac)
  coefbp(ifac) = coefbp_rij(isou,isou,ifac)
  cofbfp(ifac) = cofbfp_rij(isou,isou,ifac)
enddo

deltij = 1.0d0
if (isou.gt.3) then
  deltij = 0.0d0
endif
d1s3 = 1.d0/3.d0
d2s3 = 2.d0/3.d0

!     S as Source, V as Variable
thets  = thetst
thetv  = vcopt%thetav

call field_get_key_int(ivarfl(ivar), kstprv, st_prv_id)
if (st_prv_id.ge.0) then
  call field_get_val_v(st_prv_id, c_st_prv)
else
  c_st_prv=> null()
endif

if (st_prv_id.ge.0.and.iroext.gt.0) then
  call field_get_val_prev_s(icrom, cromo)
else
  call field_get_val_s(icrom, cromo)
endif

do iel = 1, ncel
  smbr(iel) = 0.d0
enddo
do iel = 1, ncel
  rovsdt(iel) = 0.d0
enddo

! Coefficient of the "Coriolis-type" term
if (icorio.eq.1) then
  ! Relative velocity formulation
  ccorio = 2.d0
elseif (iturbo.eq.1) then
  ! Mixed relative/absolute velocity formulation
  ccorio = 1.d0
else
  ccorio = 0.d0
endif

!===============================================================================
! 2. User source terms
!===============================================================================

call user_source_terms(ivarfl(ivar), smbr, rovsdt)

! If we extrapolate the source terms
if (st_prv_id.ge.0) then
  do iel = 1, ncel
    ! Save for exchange
    tuexpr = c_st_prv(isou,iel)
    ! For continuation and next time step
    c_st_prv(isou,iel) = smbr(iel)
    ! Right hand side of previous time step
    ! We suppose -rovsdt > 0: we implicit
    ! the user source term (the rest)
    smbr(iel) = rovsdt(iel)*cvara_var(iel) - thets*tuexpr
    ! Diagonal
    rovsdt(iel) = - thetv*rovsdt(iel)
  enddo
else
  do iel = 1, ncel
    smbr(iel)   = rovsdt(iel)*cvara_var(iel) + smbr(iel)
    rovsdt(iel) = max(-rovsdt(iel),zero)
  enddo
endif

!===============================================================================
! 3. Lagrangian source terms
!===============================================================================

!     Second order is not taken into account
if (iilagr.eq.2 .and. ltsdyn.eq.1) then
  call field_get_val_v_by_name('rij_st_lagr', lagr_st_rij)
  comp_id = isou
  if (isou .eq. 5) then
    comp_id = 6
  else if (isou .eq.6) then
    comp_id = 5
  endif
  do iel = 1,ncel
    smbr(iel)   = smbr(iel)   + lagr_st_rij(comp_id,iel)
    rovsdt(iel) = rovsdt(iel) + max(-tslagi(iel),zero)
  enddo
endif

!===============================================================================
! 4. Mass source term
!===============================================================================

if (ncesmp.gt.0) then

  ! We increment smbr with -Gamma.var_prev. and rovsdt with Gamma
  call catsmt(ncesmp, 1, icetsm, itypsm(:,irij+isou-1),                     &
              cell_f_vol, cvara_var, smacel(:,irij+isou-1), smacel(:,ipr),  &
              smbr,  rovsdt, w1)

  ! If we extrapolate the source terms we put Gamma Pinj in c_st_prv
  if (st_prv_id.ge.0) then
    do iel = 1, ncel
      c_st_prv(isou,iel) = c_st_prv(isou,iel) + w1(iel)
    enddo
  ! Otherwise we put it directly in smbr
  else
    do iel = 1, ncel
      smbr(iel) = smbr(iel) + w1(iel)
    enddo
  endif

endif

!===============================================================================
! 5. Unsteady term
!===============================================================================

do iel = 1, ncel
  rovsdt(iel) =   rovsdt(iel)                                          &
                + vcopt%istat*(crom(iel)/dt(iel))*cell_f_vol(iel)
enddo

!===============================================================================
! 6. Production, Pressure-Strain correlation, dissipation
!===============================================================================

! ---> Calculation of k for the sub-routine continuation
!       we use a work array
do iel = 1, ncel
  w8(iel) = 0.5d0 * (cvara_rij(1,iel) + cvara_rij(2,iel) + cvara_rij(3,iel))
enddo

! ---> Source term

!      (1-CRIJ2) Pij (for all components of Rij)

!      DELTAIJ*(2/3.CRIJ2.P+2/3.CRIJ1.EPSILON)
!                    (diagonal terms for R11, R22 et R33)

!      -DELTAIJ*2/3*EPSILON

!     If we extrapolate the source terms
!     We modify the implicit part:
!     In PHI1, we will only take rho CRIJ1 epsilon/k and not
!                                rho CRIJ1 epsilon/k (1-2/3 DELTAIJ)
!     It allow to keep  k^n instead of (R11^(n+1)+R22^n+R33^n)
!     This choice is questionable. It is the solution isoluc = 1
!     If we want to take all as implicit (like it is done in
!     standard first order), it is the solution isoluc = 2
!     -> to  be tested more precisely if necessary


!     If we extrapolate the source terms
if (st_prv_id.ge.0) then

  isoluc = 1

  do iel = 1, ncel

    !     Half-traces of Prod and R
    trprod = 0.5d0*(produc(1,iel)+produc(2,iel)+produc(3,iel))
    trrij  = w8(iel)

    !     Calculation of Prod+Phi1+Phi2-Eps
    !       = rhoPij-C1rho eps/k(Rij-2/3k dij)-C2rho(Pij-1/3Pkk dij)-2/3rho eps dij
    !       In c_st_prv:
    !       = rhoPij-C1rho eps/k(   -2/3k dij)-C2rho(Pij-1/3Pkk dij)-2/3rho eps dij
    !       = rho{2/3dij[C2 Pkk/2+(C1-1)eps)]+(1-C2)Pij           }
    c_st_prv(isou,iel) = c_st_prv(isou,iel) + cromo(iel) * cell_f_vol(iel)  &
                *(   deltij*d2s3*                                           &
                     (  crij2*trprod                                        &
                      +(crij1-1.d0)* cvara_ep(iel)  )                       &
                +(1.0d0-crij2)*produc(isou,iel)               )
    !       In smbr
    !       =       -C1rho eps/k(Rij         )
    !       = rho{                                     -C1eps/kRij}
    smbr(iel) = smbr(iel) + crom(iel) * cell_f_vol(iel)               &
      *( -crij1*cvara_ep(iel)/trrij * cvara_var(iel) )

    !     Calculation of the implicit part coming from Phil
    !       = C1rho eps/k(1        )
    rovsdt(iel) = rovsdt(iel) + crom(iel) * cell_f_vol(iel)           &
                            *crij1*cvara_ep(iel)/trrij*thetv

  enddo

  !     If we want to implicit a part of -C1rho eps/k(   -2/3k dij)
  if (isoluc.eq.2) then

    do iel = 1, ncel

      trrij  = w8(iel)

     !    We remove of cromo
     !       =       -C1rho eps/k(   -1/3Rij dij)
      c_st_prv(isou,iel) = c_st_prv(isou,iel) - cromo(iel) * cell_f_vol(iel)          &
                           *(deltij*d1s3*crij1*cvara_ep(iel)/trrij * cvara_var(iel))
      !    We add to smbr (with crom)
      !       =       -C1rho eps/k(   -1/3Rij dij)
      smbr(iel)                 = smbr(iel)                       &
                          + crom(iel) * cell_f_vol(iel)           &
      *(deltij*d1s3*crij1*cvara_ep(iel)/trrij * cvara_var(iel))
      !    We add to rovsdt (woth crom)
      !       =        C1rho eps/k(   -1/3    dij)
      rovsdt(iel) = rovsdt(iel) + crom(iel) * cell_f_vol(iel)         &
      *(deltij*d1s3*crij1*cvara_ep(iel)/trrij                 )
    enddo

  endif

! If we do not extrapolate the source terms
else

  do iel = 1, ncel

    !     Half-traces of Prod and R
    trprod = 0.5d0*(produc(1,iel)+produc(2,iel)+produc(3,iel))
    trrij  = w8(iel)

    !     Calculation of Prod+Phi1+Phi2-Eps
    !       = rhoPij-C1rho eps/k(Rij-2/3k dij)-C2rho(Pij-1/3Pkk dij)-2/3rho eps dij
    !       = rho{2/3dij[C2 Pkk/2+(C1-1)eps)]+(1-C2)Pij-C1eps/kRij}
    smbr(iel) = smbr(iel) + crom(iel) * cell_f_vol(iel)           &
      *(   deltij*d2s3*                                           &
           (  crij2*trprod                                        &
            +(crij1-1.d0)* cvara_ep(iel)  )                           &
         +(1.0d0-crij2)*produc(isou,iel)                          &
         -crij1*cvara_ep(iel)/trrij * cvara_var(iel)  )

    !     Calculation of the implicit part coming from Phi1
    !       = C1rho eps/k(1-1/3 dij)
    rovsdt(iel) = rovsdt(iel) + crom(iel) * cell_f_vol(iel)           &
         *(1.d0-d1s3*deltij)*crij1*cvara_ep(iel)/trrij
  enddo

endif

!===============================================================================
! 6-bis. Coriolis terms in the Phi1 and production
!===============================================================================

if (icorio.eq.1 .or. iturbo.eq.1) then

  do iel = 1, ncel
    w7(iel) = 0.d0
  enddo

  ! Index connectivity (i,j) <-> ivar
  isou_r(1,1) = 1
  isou_r(2,2) = 2
  isou_r(3,3) = 3
  isou_r(1,2) = 4
  isou_r(1,3) = 6
  isou_r(2,3) = 5
  isou_r(2,1) = isou_r(1,2)
  isou_r(3,1) = isou_r(1,3)
  isou_r(3,2) = isou_r(2,3)

  if (ivar.eq.ir11) then
    ii = 1
    jj = 1
  else if (ivar.eq.ir22) then
    ii = 2
    jj = 2
  else if (ivar.eq.ir33) then
    ii = 3
    jj = 3
  else if (ivar.eq.ir12) then
    ii = 1
    jj = 2
  else if (ivar.eq.ir13) then
    ii = 1
    jj = 3
  else if (ivar.eq.ir23) then
    ii = 2
    jj = 3
  end if

  ! Compute Gij: (i,j) component of the Coriolis production
  do kk = 1, 3
    isou_ik = isou_r(ii,kk)
    isou_jk = isou_r(jj,kk)

    do iel = 1, ncel
      call coriolis_t(irotce(iel), 1.d0, matrot)

      w7(iel) = w7(iel) - ccorio*(  matrot(ii,kk)*cvara_rij(isou_jk,iel)  &
                                  + matrot(jj,kk)*cvara_rij(isou_ik,iel))
    enddo
  enddo

  ! Coriolis contribution in the Phi1 term: (1-C2/2)Gij
  if (icorio.eq.1) then
    do iel = 1, ncel
      w7(iel) = crom(iel)*cell_f_vol(iel)*(1.d0 - 0.5d0*crij2)*w7(iel)
    enddo
  endif

  ! If source terms are extrapolated
  if (st_prv_id.ge.0) then
    do iel = 1, ncel
      c_st_prv(isou,iel) = c_st_prv(isou,iel) + w7(iel)
    enddo
  ! Otherwise, directly in smbr
  else
    do iel = 1, ncel
      smbr(iel) = smbr(iel) + w7(iel)
    enddo
  endif

endif

!===============================================================================
! 7. Wall echo terms
!===============================================================================

if (irijec.eq.1) then

  do iel = 1, ncel
    w7(iel) = 0.d0
  enddo

  call rijech(isou, produc, w7)

  ! If we extrapolate the source terms: c_st_prv
  if (st_prv_id.ge.0) then
    do iel = 1, ncel
       c_st_prv(isou,iel) = c_st_prv(isou,iel) + w7(iel)
     enddo
  ! Otherwise smbr
  else
    do iel = 1, ncel
      smbr(iel) = smbr(iel) + w7(iel)
    enddo
  endif

endif

!===============================================================================
! 8. Buoyancy source term
!===============================================================================

if (igrari.eq.1) then

  do iel = 1, ncel
    w7(iel) = 0.d0
  enddo

  call rijthe(nscal, ivar, gradro, w7)

  ! If source terms are extrapolated
  if (st_prv_id.ge.0) then
    do iel = 1, ncel
      c_st_prv(isou,iel) = c_st_prv(isou,iel) + w7(iel)
    enddo
  else
    do iel = 1, ncel
      smbr(iel) = smbr(iel) + w7(iel)
    enddo
  endif

endif

!===============================================================================
! 9. Diffusion term (Daly Harlow: generalized gradient hypothesis method)
!===============================================================================

! Symmetric tensor diffusivity (GGDH)
if (iand(vcopt%idften, ANISOTROPIC_DIFFUSION).ne.0) then

  call field_get_val_v(ivsten, visten)

  do iel = 1, ncel
    viscce(1,iel) = visten(1,iel) + viscl(iel)
    viscce(2,iel) = visten(2,iel) + viscl(iel)
    viscce(3,iel) = visten(3,iel) + viscl(iel)
    viscce(4,iel) = visten(4,iel)
    viscce(5,iel) = visten(5,iel)
    viscce(6,iel) = visten(6,iel)
  enddo

  iwarnp = vcopt%iwarni

  call vitens &
 ( viscce , iwarnp ,             &
   weighf , weighb ,             &
   viscf  , viscb  )

! Scalar diffusivity
else

  do iel = 1, ncel
    trrij = 0.5d0 * (cvara_rij(1,iel) + cvara_rij(2,iel) + cvara_rij(3,iel))
    rctse = crom(iel) * csrij * trrij**2 / cvara_ep(iel)
    w1(iel) = viscl(iel) + vcopt%idifft*rctse
  enddo

  imvisp = vcopt%imvisf

  call viscfa                    &
 ( imvisp ,                      &
   w1     ,                      &
   viscf  , viscb  )

endif

!===============================================================================
! 10. Solving
!===============================================================================

if (st_prv_id.ge.0) then
  thetp1 = 1.d0 + thets
  do iel = 1, ncel
    smbr(iel) = smbr(iel) + thetp1*c_st_prv(isou,iel)
  enddo
endif

iescap = 0
imucpp = 0
! all boundary convective flux with upwind
icvflb = 0
normp = -1.d0
init   = 1

c_name = trim(nomva0)//c_null_char

vcopt_loc = vcopt

vcopt_loc%istat  = -1
vcopt_loc%icoupl = -1
vcopt_loc%idifft = -1
vcopt_loc%iwgrec = 0 ! Warning, may be overwritten if a field
vcopt_loc%thetav = thetv
vcopt_loc%blend_st = 0 ! Warning, may be overwritten if a field

p_k_value => vcopt_loc
c_k_value = equation_param_from_vcopt(c_loc(p_k_value))

call cs_equation_iterative_solve_scalar          &
 ( idtvar , init   ,                             &
   ivarfl(ivar)    , c_name ,                    &
   iescap , imucpp , normp  , c_k_value       ,  &
   cvara_var       , cvara_var       ,           &
   coefap , coefbp , cofafp , cofbfp ,           &
   imasfl , bmasfl ,                             &
   viscf  , viscb  , viscf  , viscb  ,           &
   viscce , weighf , weighb ,                    &
   icvflb , ivoid  ,                             &
   rovsdt , smbr   , cvar_var        , dpvar  ,  &
   rvoid  , rvoid  )

! Retrieve solution component

do iel = 1, ncel
  cvar_rij(isou,iel) = cvar_var(iel)
enddo

! Free memory

deallocate(cvar_var, cvara_var)
deallocate(coefap, coefbp)
deallocate(cofafp, cofbfp)

deallocate(w1)
deallocate(w7, w8)
deallocate(dpvar)
deallocate(viscce)
deallocate(weighf, weighb)

!--------
! Formats
!--------

 1000 format(/,'           Solving variable ', a8          ,/)

!----
! End
!----

return

end subroutine
