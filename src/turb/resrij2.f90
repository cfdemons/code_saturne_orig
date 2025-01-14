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

!> \file resrij2.f90
!>
!> \brief This subroutine performs the solving of the coupled Reynolds stress
!> components in \f$ R_{ij} - \varepsilon \f$ RANS (LRR) turbulence model.
!>
!> \remark
!> - cvar_var(1,*) for \f$ R_{11} \f$
!> - cvar_var(2,*) for \f$ R_{22} \f$
!> - cvar_var(3,*) for \f$ R_{33} \f$
!> - cvar_var(4,*) for \f$ R_{12} \f$
!> - cvar_var(5,*) for \f$ R_{23} \f$
!> - cvar_var(6,*) for \f$ R_{13} \f$
!-------------------------------------------------------------------------------

!-------------------------------------------------------------------------------
! Arguments
!______________________________________________________________________________.
!  mode           name          role
!______________________________________________________________________________!
!> \param[in]     nvar          total number of variables
!> \param[in]     nscal         total number of scalars
!> \param[in]     ncepdp        number of cells with head loss
!> \param[in]     ncesmp        number of cells with mass source term
!> \param[in]     icepdc        index of cells with head loss
!> \param[in]     icetsm        index of cells with mass source term
!> \param[in]     itypsm        type of mass source term for each variable
!>                               (see \ref cs_user_mass_source_terms)
!> \param[in]     dt            time step (per cell)
!> \param[in]     gradv         work array for the velocity grad term
!>                                 only for iturb=31
!> \param[in]     produc        work array for production
!> \param[in]     gradro        work array for grad rom
!>                              (without rho volume) only for iturb=30
!> \param[in]     ckupdc        work array for the head loss
!> \param[in]     smacel        value associated to each variable in the mass
!>                               source terms or mass rate (see \ref cs_user_mass_source_terms)
!> \param[in]     viscf         visc*surface/dist at internal faces
!> \param[in]     viscb         visc*surface/dist at edge faces
!> \param[in]     tslagi        implicit source terms for the Lagrangian module
!> \param[in]     smbr          working array
!> \param[in]     rovsdt        working array
!______________________________________________________________________________!

subroutine resrij2 &
 ( nvar   , nscal  , ncepdp , ncesmp ,                            &
   icepdc , icetsm , itypsm ,                                     &
   dt     ,                                                       &
   gradv  , produc , gradro ,                                     &
   ckupdc , smacel ,                                              &
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
integer          ncepdp , ncesmp

integer          icepdc(ncepdp)
integer          icetsm(ncesmp), itypsm(ncesmp,nvar)

double precision dt(ncelet)
double precision gradv(3, 3, ncelet)
double precision produc(6,ncelet)
double precision gradro(3,ncelet)
double precision ckupdc(6,ncepdp), smacel(ncesmp,nvar)
double precision viscf(nfac), viscb(nfabor)
double precision tslagi(ncelet)
double precision smbr(6,ncelet), rovsdt(6,6,ncelet)

! Local variables

integer          iel
integer          isou, jsou
integer          ii    , jj    , kk
integer          iflmas, iflmab
integer          iwarnp
integer          imvisp
integer          st_prv_id
integer          icvflb
integer          ivoid(1)
integer          dimrij, f_id
integer          key_t_ext_id
integer          iroext

double precision trprod, trrij , deltij(6)
double precision tuexpr, thets , thetv , thetp1
double precision d1s3  , d2s3
double precision ccorio, matrot(3,3)
double precision rctse

character(len=80) :: label
double precision, allocatable, dimension(:,:), target :: buoyancy
double precision, allocatable, dimension(:) :: w1
double precision, allocatable, dimension(:,:) :: w7
double precision, allocatable, dimension(:) :: dpvar
double precision, allocatable, dimension(:,:) :: viscce
double precision, allocatable, dimension(:,:) :: weighf
double precision, allocatable, dimension(:) :: weighb
double precision, dimension(:), pointer :: imasfl, bmasfl
double precision, dimension(:), pointer :: crom, cromo
double precision, dimension(:,:), pointer :: coefap, cofafp
double precision, dimension(:,:,:), pointer :: coefbp, cofbfp
double precision, dimension(:,:), pointer :: visten
double precision, dimension(:), pointer :: cvara_ep
double precision, dimension(:,:), pointer :: cvar_var, cvara_var
double precision, allocatable, dimension(:,:) :: cvara_r
double precision, dimension(:,:), pointer :: c_st_prv, lagr_st_rij
double precision, dimension(:), pointer :: viscl
double precision, dimension(:,:), pointer :: cpro_buoyancy

integer iii,jjj
integer t2v(3,3)

double precision impl_drsm(6,6)
double precision implmat2add(3,3)
double precision impl_lin_cst, impl_id_cst
double precision aiksjk, aikrjk, aii ,aklskl, aikakj
double precision xaniso(3,3), xstrai(3,3), xrotac(3,3), xprod(3,3)
double precision sym_strain(6)
double precision matrn(6), oo_matrn(6)
double precision eigen_vals(3)
double precision ceps_impl
double precision eigen_max
double precision pij, phiij1, phiij2, epsij
double precision d1s2

type(var_cal_opt) :: vcopt_rij
type(var_cal_opt), target   :: vcopt_loc
type(var_cal_opt), pointer  :: p_k_value
type(c_ptr)                 :: c_k_value

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
allocate(w7(6,ncelet))
allocate(dpvar(ncelet))
allocate(viscce(6,ncelet))
allocate(weighf(2,nfac))
allocate(weighb(nfabor))

call field_get_key_struct_var_cal_opt(ivarfl(irij), vcopt_rij)

if (vcopt_rij%iwarni.ge.1) then
  call field_get_label(ivarfl(irij), label)
  write(nfecra,1000) label
endif

call field_get_val_s(icrom, crom)
call field_get_val_s(iviscl, viscl)
call field_get_key_int(ivarfl(iu), kimasf, iflmas)
call field_get_key_int(ivarfl(iu), kbmasf, iflmab)
call field_get_val_s(iflmas, imasfl)
call field_get_val_s(iflmab, bmasfl)

call field_get_val_prev_s(ivarfl(iep), cvara_ep)


call field_get_val_v(ivarfl(irij), cvar_var)
call field_get_val_prev_v(ivarfl(irij), cvara_var)
call field_get_dim(ivarfl(irij),dimrij)! dimension of Rij

call field_get_coefa_v(ivarfl(irij), coefap)
call field_get_coefb_v(ivarfl(irij), coefbp)
call field_get_coefaf_v(ivarfl(irij), cofafp)
call field_get_coefbf_v(ivarfl(irij), cofbfp)

do isou = 1, 6
  deltij(isou) = 1.0d0
  if (isou.gt.3) then
    deltij(isou) = 0.0d0
  endif
enddo
d1s3 = 1.d0/3.d0
d2s3 = 2.d0/3.d0

!     S as Source, V as Variable
thets  = thetst
thetv  = vcopt_rij%thetav

call field_get_key_int(ivarfl(irij), kstprv, st_prv_id)
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

d1s2   = 1.d0/2.d0

t2v(1,1) = 1; t2v(1,2) = 4; t2v(1,3) = 6;
t2v(2,1) = 4; t2v(2,2) = 2; t2v(2,3) = 5;
t2v(3,1) = 6; t2v(3,2) = 5; t2v(3,3) = 3;

!===============================================================================
! 2. User source terms
!===============================================================================

! If we extrapolate the source terms
if (st_prv_id.ge.0) then
  do iel = 1, ncel
    do isou = 1, dimrij
      ! Save for exchange
      tuexpr = c_st_prv(isou,iel)
      ! For continuation and the next time step
      c_st_prv(isou,iel) = smbr(isou,iel)
      ! Second member of the previous time step
      ! We suppose -rovsdt > 0: we implicite
      !    the user source term (the rest)
      do jsou = 1, dimrij
        smbr(isou,iel) = rovsdt(jsou,isou,iel)*cvara_var(jsou,iel)  - thets*tuexpr
        ! Diagonal
        rovsdt(jsou,isou,iel) = - thetv*rovsdt(jsou,isou,iel)
      enddo
    enddo
  enddo
else
  do iel = 1, ncel
    do isou = 1, dimrij
      do jsou = 1, dimrij
        smbr(isou,iel)   = rovsdt(jsou,isou,iel)*cvara_var(jsou,iel) + smbr(isou,iel)
      enddo
      rovsdt(isou,isou,iel) = max(-rovsdt(isou,isou,iel), 0.d0)
    enddo
  enddo
endif

!===============================================================================
! 3. Lagrangian source terms
!===============================================================================

!     2nd order is not taken into account
if (iilagr.eq.2 .and. ltsdyn.eq.1) then
  call field_get_val_v_by_name('rij_st_lagr', lagr_st_rij)
  do iel = 1,ncel
    do isou = 1, dimrij
      smbr(isou, iel) = smbr(isou, iel) + lagr_st_rij(isou,iel)
      rovsdt(isou,isou,iel) = rovsdt(isou,isou, iel) + max(-tslagi(iel),zero)
    enddo
  enddo
endif

!===============================================================================
! 4. Mass source term
!===============================================================================

if (ncesmp.gt.0) then

  do isou = 1, dimrij

    ! We increment smbr with -Gamma.var_prev and rovsdr with Gamma
    call catsmt(ncesmp, 1, icetsm, itypsm(:,irij+isou-1),                     &
                cell_f_vol, cvara_var, smacel(:,irij+isou-1), smacel(:,ipr),  &
                smbr, rovsdt, w1)

    ! If we extrapolate the source terms we put Gamma Pinj in the previous st
    if (st_prv_id.ge.0) then
      do iel = 1, ncel
        c_st_prv(isou,iel) = c_st_prv(isou,iel) + w1(iel)
      enddo
    ! Otherwise we put it directly in the RHS
    else
      do iel = 1, ncel
        smbr(isou, iel) = smbr(isou, iel) + w1(iel)
      enddo
    endif

  enddo
endif

!===============================================================================
! 5. Unsteady term
!===============================================================================

! ---> Added in the matrix diagonal

do iel=1,ncel
  do isou = 1, dimrij
    rovsdt(isou,isou,iel) = rovsdt(isou,isou,iel)                              &
              + vcopt_rij%istat*(crom(iel)/dt(iel))*cell_f_vol(iel)
  enddo
enddo


!===============================================================================
! 6. Production, Pressure-Strain correlation, dissipation
!===============================================================================

do iel = 1, ncel

  ! Initalize implicit matrices at 0
  do isou = 1, 6
    do jsou = 1, 6
      impl_drsm(isou, jsou) = 0.0d0
    end do
  end do
  do isou = 1, 3
    do jsou = 1, 3
      implmat2add(isou, jsou) = 0.0d0
    end do
  end do

  impl_lin_cst = 0.0d0
  impl_id_cst  = 0.0d0

  ! Pij
  xprod(1,1) = produc(1, iel)
  xprod(1,2) = produc(4, iel)
  xprod(1,3) = produc(6, iel)
  xprod(2,2) = produc(2, iel)
  xprod(2,3) = produc(5, iel)
  xprod(3,3) = produc(3, iel)

  xprod(2,1) = xprod(1,2)
  xprod(3,1) = xprod(1,3)
  xprod(3,2) = xprod(2,3)

  trprod = d1s2 * (xprod(1,1) + xprod(2,2) + xprod(3,3) )
  trrij  = d1s2 * (cvara_var(1 ,iel) + cvara_var(2 ,iel) + cvara_var(3 ,iel))

  !-----> aII = aijaij
  aii    = 0.d0
  aklskl = 0.d0
  aiksjk = 0.d0
  aikrjk = 0.d0
  aikakj = 0.d0
  ! aij
  xaniso(1,1) = cvara_var(1 ,iel)/trrij - d2s3
  xaniso(2,2) = cvara_var(2 ,iel)/trrij - d2s3
  xaniso(3,3) = cvara_var(3 ,iel)/trrij - d2s3
  xaniso(1,2) = cvara_var(4 ,iel)/trrij
  xaniso(1,3) = cvara_var(6 ,iel)/trrij
  xaniso(2,3) = cvara_var(5 ,iel)/trrij
  xaniso(2,1) = xaniso(1,2)
  xaniso(3,1) = xaniso(1,3)
  xaniso(3,2) = xaniso(2,3)
  ! Sij
  xstrai(1,1) = gradv(1, 1, iel)
  xstrai(1,2) = d1s2*(gradv(2, 1, iel)+gradv(1, 2, iel))
  xstrai(1,3) = d1s2*(gradv(3, 1, iel)+gradv(1, 3, iel))
  xstrai(2,1) = xstrai(1,2)
  xstrai(2,2) = gradv(2, 2, iel)
  xstrai(2,3) = d1s2*(gradv(3, 2, iel)+gradv(2, 3, iel))
  xstrai(3,1) = xstrai(1,3)
  xstrai(3,2) = xstrai(2,3)
  xstrai(3,3) = gradv(3, 3, iel)
  sym_strain(1) = xstrai(1,1)
  sym_strain(2) = xstrai(2,2)
  sym_strain(3) = xstrai(3,3)
  sym_strain(4) = xstrai(1,2)
  sym_strain(5) = xstrai(2,3)
  sym_strain(6) = xstrai(1,3)
  ! omegaij
  xrotac(1,1) = 0.d0
  xrotac(1,2) = d1s2*(gradv(2, 1, iel)-gradv(1, 2, iel))
  xrotac(1,3) = d1s2*(gradv(3, 1, iel)-gradv(1, 3, iel))
  xrotac(2,1) = -xrotac(1,2)
  xrotac(2,2) = 0.d0
  xrotac(2,3) = d1s2*(gradv(3, 2, iel)-gradv(2, 3, iel))
  xrotac(3,1) = -xrotac(1,3)
  xrotac(3,2) = -xrotac(2,3)
  xrotac(3,3) = 0.d0

  do ii=1,3
    do jj = 1,3
      ! aii = aij.aij
      aii    = aii+xaniso(ii,jj)*xaniso(ii,jj)
      ! aklskl = aij.Sij
      aklskl = aklskl + xaniso(ii,jj)*xstrai(ii,jj)
    enddo
  enddo

  ! Computation of implicit components

  do isou = 1, 6
    matrn(isou) = cvara_var(isou,iel)/trrij
    oo_matrn(isou) = 0.0d0
  end do

  ! Inversing the matrix
  call symmetric_matrix_inverse(matrn, oo_matrn)
  do isou = 1, dimrij
    oo_matrn(isou) = oo_matrn(isou)/trrij
  end do

  ! Computing the maximal eigenvalue (in terms of norm!) of S
  call calc_symtens_eigvals(sym_strain, eigen_vals)
  eigen_max = maxval(abs(eigen_vals))

  ! Constant for the dissipation
  ceps_impl = d1s3 * cvara_ep(iel)

  ! Identity constant
  impl_id_cst = -d1s3*crij2*min(trprod,0.0d0)

  ! Linear constant
  impl_lin_cst = eigen_max *     ( &
                 (1.d0-crij2)    )   ! Production + Phi2

  do jsou = 1, 3
    do isou = 1 ,3
      iii = t2v(isou,jsou)
      implmat2add(isou,jsou) = (1.d0-crij2)*xrotac(isou,jsou)              &
                             + impl_lin_cst*deltij(iii)       &
                             + impl_id_cst*d1s2*oo_matrn(iii) &
                             + ceps_impl*oo_matrn(iii)
    end do
  end do

  impl_drsm(:,:) = 0.0d0
  call reduce_symprod33_to_66(implmat2add, impl_drsm)

  ! Rotating frame of reference => "absolute" vorticity
  if (icorio.eq.1) then
    do ii = 1, 3
      do jj = 1, 3
        xrotac(ii,jj) = xrotac(ii,jj) + matrot(ii,jj)
      enddo
    enddo
  endif

  do isou = 1, dimrij
    if (isou.eq.1)then
      iii = 1
      jjj = 1
    elseif (isou.eq.2)then
      iii = 2
      jjj = 2
    elseif (isou.eq.3)then
      iii = 3
      jjj = 3
    elseif (isou.eq.4)then
      iii = 1
      jjj = 2
    elseif (isou.eq.5)then
      iii = 2
      jjj = 3
    elseif (isou.eq.6)then
      iii = 1
      jjj = 3
    endif
    aiksjk = 0
    aikrjk = 0
    aikakj = 0
    do kk = 1,3
      ! aiksjk = aik.Sjk+ajk.Sik
      aiksjk = aiksjk + xaniso(iii,kk)*xstrai(jjj,kk)              &
                +xaniso(jjj,kk)*xstrai(iii,kk)
      ! aikrjk = aik.Omega_jk + ajk.omega_ik
      aikrjk = aikrjk + xaniso(iii,kk)*xrotac(jjj,kk)              &
                +xaniso(jjj,kk)*xrotac(iii,kk)
      ! aikakj = aik*akj
      aikakj = aikakj + xaniso(iii,kk)*xaniso(kk,jjj)
    enddo

    ! Explicit terms
    pij = (1.d0 - crij2)*xprod(iii,jjj)
    phiij1 = -cvara_ep(iel)*crij1*xaniso(iii,jjj)
    phiij2 = d2s3*crij2*trprod*deltij(isou)
    epsij = -d2s3*cvara_ep(iel)*deltij(isou)

    if (st_prv_id.ge.0) then
      c_st_prv(isou,iel) = c_st_prv(isou,iel) &
        + cromo(iel)*cell_f_vol(iel)*(pij+phiij1+phiij2+epsij)
    else
      smbr(isou,iel) = smbr(isou,iel) &
        + cromo(iel)*cell_f_vol(iel)*(pij+phiij1+phiij2+epsij)
      ! Implicit terms
      rovsdt(isou,isou,iel) = rovsdt(isou,isou,iel) &
        + cell_f_vol(iel)/trrij*crom(iel)*(crij1*cvara_ep(iel))

      ! Carefull ! Inversion of the order of the coefficients since
      ! rovsdt matrix is then used by a c function for the linear solving
      do jsou = 1, 6
        rovsdt(jsou,isou,iel) = rovsdt(jsou,isou,iel) + cell_f_vol(iel) &
                                *crom(iel) * impl_drsm(isou,jsou)
      end do

    endif

  enddo

enddo



!===============================================================================
! 6-bis. Coriolis terms in the Phi1 and production
!===============================================================================

if (icorio.eq.1 .or. iturbo.eq.1) then
  allocate(cvara_r(3,3))
  do iel = 1, ncel
    do isou = 1, 6
      w7(isou,iel) = 0.d0
    enddo
  enddo

  do iel = 1, ncel
    cvara_r(1,1) = cvara_var(1,iel)
    cvara_r(2,2) = cvara_var(2,iel)
    cvara_r(3,3) = cvara_var(3,iel)
    cvara_r(1,2) = cvara_var(4,iel)
    cvara_r(2,3) = cvara_var(5,iel)
    cvara_r(1,3) = cvara_var(6,iel)
    cvara_r(2,1) = cvara_var(4,iel)
    cvara_r(3,2) = cvara_var(5,iel)
    cvara_r(3,1) = cvara_var(6,iel)
    ! Compute Gij: (i,j) component of the Coriolis production
    do isou = 1, 6
      if (isou.eq.1) then
        ii = 1
        jj = 1
      else if (isou.eq.2) then
        ii = 2
        jj = 2
      else if (isou.eq.3) then
        ii = 3
        jj = 3
      else if (isou.eq.4) then
        ii = 1
        jj = 2
      else if (isou.eq.5) then
        ii = 2
        jj = 3
      else if (isou.eq.6) then
        ii = 1
        jj = 3
      end if
      do kk = 1, 3

        call coriolis_t(irotce(iel), 1.d0, matrot)

        w7(isou,iel) = w7(isou,iel) - ccorio*(  matrot(ii,kk)*cvara_r(jj,kk) &
                                    + matrot(jj,kk)*cvara_r(ii,kk) )
      enddo
    enddo
  enddo

  ! Coriolis contribution in the Phi1 term: (1-C2/2)Gij
  if (icorio.eq.1) then
    do iel = 1, ncel
      do isou = 1, 6
        w7(isou,iel) = crom(iel)*cell_f_vol(iel)*(1.d0 - 0.5d0*crij2)*w7(isou,iel)
      enddo
    enddo
  endif

  ! If source terms are extrapolated
  if (st_prv_id.ge.0) then
    do iel = 1, ncel
      do isou = 1, 6
        c_st_prv(isou,iel) = c_st_prv(isou,iel) + w7(isou,iel)
      enddo
    enddo
  ! Otherwise, directly in smbr
  else
    do iel = 1, ncel
      do isou = 1, 6
        smbr(isou,iel) = smbr(isou,iel) + w7(isou,iel)
      enddo
    enddo
  endif

endif

!===============================================================================
! 7. Wall echo terms
!===============================================================================

if (irijec.eq.1) then !todo

  do iel = 1, ncel
    do isou = 1, 6
      w7(isou,iel) = 0.d0
    enddo
  enddo

  call rijech2(produc, w7)

  ! If we extrapolate the source terms: c_st_prv
  if (st_prv_id.ge.0) then
    do iel = 1, ncel
      do isou = 1, 6
        c_st_prv(isou,iel) = c_st_prv(isou,iel) + w7(isou,iel)
      enddo
    enddo
  ! Otherwise smbr
  else
    do iel = 1, ncel
      do isou = 1, 6
        smbr(isou,iel) = smbr(isou,iel) + w7(isou,iel)
      enddo
    enddo
  endif

endif


!===============================================================================
! 8. Buoyancy source term
!===============================================================================

if (igrari.eq.1) then

  call field_get_id_try("rij_buoyancy", f_id)
  if (f_id.ge.0) then
    call field_get_val_v(f_id, cpro_buoyancy)
  else
    ! Allocate a work array
    allocate(buoyancy(6,ncelet))
    cpro_buoyancy => buoyancy
  endif

  call rijthe2(nscal, gradro, cpro_buoyancy)

  ! If we extrapolate the source terms: previous ST
  if (st_prv_id.ge.0) then
    do iel = 1, ncel
      do isou = 1, dimrij
        c_st_prv(isou,iel) = c_st_prv(isou,iel) + cpro_buoyancy(isou,iel) * cell_f_vol(iel)
      enddo
    enddo
    ! Otherwise smbr
  else
    do iel = 1, ncel
      do isou = 1, dimrij
        smbr(isou,iel) = smbr(isou,iel) + cpro_buoyancy(isou,iel) * cell_f_vol(iel)
      enddo
    enddo
  endif

  ! Free memory
  if (allocated(buoyancy)) deallocate(buoyancy)

endif

!===============================================================================
! 9. Diffusion term (Daly Harlow: generalized gradient hypothesis method)
!===============================================================================

! Symmetric tensor diffusivity (GGDH)
if (iand(vcopt_rij%idften, ANISOTROPIC_RIGHT_DIFFUSION).ne.0) then

  call field_get_val_v(ivsten, visten)

  do iel = 1, ncel
    viscce(1,iel) = visten(1,iel) + viscl(iel)
    viscce(2,iel) = visten(2,iel) + viscl(iel)
    viscce(3,iel) = visten(3,iel) + viscl(iel)
    viscce(4,iel) = visten(4,iel)
    viscce(5,iel) = visten(5,iel)
    viscce(6,iel) = visten(6,iel)
  enddo

  iwarnp = vcopt_rij%iwarni

  call vitens &
 ( viscce , iwarnp ,             &
   weighf , weighb ,             &
   viscf  , viscb  )

! Scalar diffusivity
else

  do iel = 1, ncel
    trrij = 0.5d0 * (cvara_var(1,iel) + cvara_var(2,iel) + cvara_var(3,iel))
    rctse = crom(iel) * csrij * trrij**2 / cvara_ep(iel)
    w1(iel) = viscl(iel) + vcopt_rij%idifft*rctse
  enddo

  imvisp = vcopt_rij%imvisf

  call viscfa(imvisp, w1, viscf, viscb)

endif

!===============================================================================
! 10. Solving
!===============================================================================

if (st_prv_id.ge.0) then
  thetp1 = 1.d0 + thets
  do iel = 1, ncel
    do isou = 1, dimrij
      smbr(isou,iel) = smbr(isou,iel) + thetp1*c_st_prv(isou,iel)
    enddo
  enddo
endif

! all boundary convective flux with upwind
icvflb = 0

! Fromcs_c_bindings
c_name = trim(nomva0)//c_null_char

vcopt_loc = vcopt_rij

vcopt_loc%istat  = -1
vcopt_loc%idifft = -1
vcopt_loc%iwgrec = 0
vcopt_loc%thetav = thetv
vcopt_loc%blend_st = 0 ! Warning, may be overwritten if a field
vcopt_loc%extrag = 0

p_k_value => vcopt_loc
c_k_value = equation_param_from_vcopt(c_loc(p_k_value))

call cs_equation_iterative_solve_tensor           &
 ( idtvar , ivarfl(irij)    , c_name ,            &
   c_k_value,                                     &
   cvara_var       , cvara_var       ,            &
   coefap , coefbp , cofafp , cofbfp ,            &
   imasfl , bmasfl , viscf  ,                     &
   viscb  , viscf  , viscb  , viscce ,            &
   weighf , weighb , icvflb , ivoid  ,            &
   rovsdt , smbr   , cvar_var        )

! Free memory
deallocate(w1)
deallocate(w7)
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
