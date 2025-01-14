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
! Function :
! ----------
!> \file resalp.f90
!> \brief Solving the equation on alpha in the framwork of the Rij-EBRSM model.
!>        Also called for alpha of scalars for EB-DFM.

!-------------------------------------------------------------------------------

!-------------------------------------------------------------------------------
! Arguments
!______________________________________________________________________________.
!  mode           nom           role
!______________________________________________________________________________!
!> \param[in]     f_id          field id of alpha variable
!> \param[in]     c_durbin_l    constant for the Durbin length
!______________________________________________________________________________!

subroutine resalp(f_id, c_durbin_l )

!===============================================================================
! Module files
!===============================================================================

use paramx
use numvar
use entsor
use optcal
use cstnum
use cstphy
use pointe
use period
use parall
use mesh
use field
use cs_c_bindings

!===============================================================================

implicit none

! Arguments
integer          f_id
double precision c_durbin_l

! Local variables

integer          iel
integer          ii    , jj    , ifac
integer          iflmas, iflmab
integer          imvisp
integer          iescap
integer          imucpp
integer          icvflb
integer          init
integer          ivoid(1)
double precision thetv , thetap
double precision d1s4, d3s2, d1s2
double precision xk, xnu, l2
double precision xllke, xllkmg, xlldrb
double precision normp

double precision rvoid(1)

type(var_cal_opt), target   :: vcopt
type(var_cal_opt), target :: vcopt_loc
type(var_cal_opt), pointer :: p_k_value
type(c_ptr) :: c_k_value

character(len=80) :: label
double precision, allocatable, dimension(:) :: viscf, viscb
double precision, allocatable, dimension(:) :: alpha_min
double precision, allocatable, dimension(:) :: smbr, rovsdt
double precision, allocatable, dimension(:) :: w1
double precision, allocatable, dimension(:) :: dpvar
double precision, dimension(:), pointer :: imasfl, bmasfl
double precision, dimension(:), pointer :: crom
double precision, dimension(:), pointer :: coefap, coefbp, cofafp, cofbfp
double precision, dimension(:), pointer :: cvar_al, cvara_al, cvara_ep
double precision, dimension(:), pointer :: viscl, visct
double precision, dimension(:,:), pointer :: cvara_rij

character(len=len_trim(nomva0)+1, kind=c_char) :: c_name

!===============================================================================

!===============================================================================
! 1. Initialization
!===============================================================================

allocate(smbr(ncelet), rovsdt(ncelet), w1(ncelet))
allocate(viscf(nfac), viscb(nfabor))
allocate(dpvar(ncelet))

call field_get_val_s(icrom, crom)
call field_get_val_s(iviscl, viscl)
call field_get_val_s(ivisct, visct)

call field_get_val_s(f_id, cvar_al)
call field_get_val_prev_s(f_id, cvara_al)
call field_get_val_prev_s(ivarfl(iep), cvara_ep)
call field_get_val_prev_v(ivarfl(irij), cvara_rij)

call field_get_key_int(ivarfl(iu), kimasf, iflmas)
call field_get_key_int(ivarfl(iu), kbmasf, iflmab)
call field_get_val_s(iflmas, imasfl)
call field_get_val_s(iflmab, bmasfl)

d1s2 = 1.d0/2.d0
d1s4 = 1.d0/4.d0
d3s2 = 3.d0/2.d0

!===============================================================================
! 2. Resolving the equation of alpha
!===============================================================================

! Get calculation options
call field_get_key_struct_var_cal_opt(f_id, vcopt)

call field_get_coefa_s (f_id, coefap)
call field_get_coefb_s (f_id, coefbp)
call field_get_coefaf_s(f_id, cofafp)
call field_get_coefbf_s(f_id, cofbfp)

if(vcopt%iwarni.ge.1) then
  call field_get_label(f_id, label)
  write(nfecra,1100) label
endif

thetv  = vcopt%thetav

do iel = 1, ncel
  smbr(iel) = 0.d0
enddo
do iel = 1, ncel
  rovsdt(iel) = 0.d0
enddo

!===============================================================================
! 2.2 Source term of alpha
!     \f$ smbr = \dfrac{1}{L^2 (\alpha)} - \dfrac{1}{L^2}\f$
!  In fact there is a mark "-" because the solved equation is
!    \f$-\div{\grad {alpha}} = smbr \f$
!===============================================================================

! ---> Matrix

if (isto2t.gt.0) then
  thetap = thetv
else
  thetap = 1.d0
endif

!FIXME the source term extrapolation is not well done!!!!
do iel = 1, ncel
   xk = d1s2*(cvara_rij(1,iel)+cvara_rij(2,iel)+cvara_rij(3,iel))
   xnu  = viscl(iel)/crom(iel)

  ! Integral length scale
  xllke = xk**d3s2/cvara_ep(iel)

  ! Kolmogorov length scale
  xllkmg = xceta*(xnu**3/cvara_ep(iel))**d1s4

  ! Durbin length scale
  xlldrb = c_durbin_l * max(xllke,xllkmg)

  ! For automatic initialization, the length scale is fixed at L^+ =50
  if (ntcabs.eq.1.and.reinit_turb.eq.1) xlldrb=50.d0*viscl0/ro0/(0.05d0*uref)

  l2 = xlldrb**2

  ! Explicit term
  smbr(iel) = cell_f_vol(iel)*(1.d0 -cvara_al(iel)) / l2

  ! Implicit term
  rovsdt(iel) = (rovsdt(iel) + cell_f_vol(iel)*thetap) / l2

enddo

! Calculation of viscf and viscb for cs_equation_iterative_solve_scalar.

do iel = 1, ncel
  w1(iel) = 1.d0
enddo

imvisp = vcopt%imvisf

call viscfa                                                       &
 ( imvisp ,                                                       &
   w1     ,                                                       &
   viscf  , viscb  )

!===============================================================================
! 2.3 Effective resolution of the equation of alpha
!===============================================================================

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
   f_id   , c_name ,                             &
   iescap , imucpp , normp  , c_k_value       ,  &
   cvara_al        , cvara_al        ,           &
   coefap , coefbp , cofafp , cofbfp ,           &
   imasfl , bmasfl ,                             &
   viscf  , viscb  , viscf  , viscb  ,           &
   rvoid  , rvoid  , rvoid  ,                    &
   icvflb , ivoid  ,                             &
   rovsdt , smbr   , cvar_al, dpvar  ,           &
   rvoid  , rvoid  )

!===============================================================================
! 3. Clipping
!===============================================================================

allocate(alpha_min(ncelet))

! Compute a first estimator of the minimal value of alpha per cell.
! This is deduced from "alpha/L^2 - div(grad alpha) = 1/L^2" and assuming that
! boundary cell values are 0. This value is thefore non zero but
! much smaller than the wanted value.
do iel = 1, ncel
  alpha_min(iel) = rovsdt(iel)
enddo

do iel = ncel +1, ncelet
  alpha_min(iel) = 0.d0
enddo

do ifac = 1, nfac
  ii = ifacel(1, ifac)
  jj = ifacel(2, ifac)
  alpha_min(ii) = alpha_min(ii) + viscf(ifac)
  alpha_min(jj) = alpha_min(jj) + viscf(ifac)
enddo

do ifac = 1, nfabor
  ii = ifabor(ifac)
  alpha_min(ii) = alpha_min(ii) + viscb(ifac)/distb(ifac)
enddo

do iel = 1, ncel
  alpha_min(iel) = rovsdt(iel)/alpha_min(iel)
enddo

call clpalp(f_id, ncelet, ncel, alpha_min)

! Free memory
deallocate(smbr, rovsdt, w1)
deallocate(viscf, viscb,alpha_min)
deallocate(dpvar)

!--------
! Formats
!--------

 1100    format(/,'           Solving the variable ',A8,/)

!----
! End
!----

return

end
