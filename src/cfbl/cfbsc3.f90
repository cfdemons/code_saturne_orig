!-------------------------------------------------------------------------------

!     This file is part of the Code_Saturne Kernel, element of the
!     Code_Saturne CFD tool.

!     Copyright (C) 1998-2009 EDF S.A., France

!     contact: saturne-support@edf.fr

!     The Code_Saturne Kernel is free software; you can redistribute it
!     and/or modify it under the terms of the GNU General Public License
!     as published by the Free Software Foundation; either version 2 of
!     the License, or (at your option) any later version.

!     The Code_Saturne Kernel is distributed in the hope that it will be
!     useful, but WITHOUT ANY WARRANTY; without even the implied warranty
!     of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
!     GNU General Public License for more details.

!     You should have received a copy of the GNU General Public License
!     along with the Code_Saturne Kernel; if not, write to the
!     Free Software Foundation, Inc.,
!     51 Franklin St, Fifth Floor,
!     Boston, MA  02110-1301  USA

!-------------------------------------------------------------------------------

subroutine cfbsc3 &
!================

 ( idbia0 , idbra0 ,                                              &
   ndim   , ncelet , ncel   , nfac   , nfabor , nfml   , nprfml , &
   nnod   , lndfac , lndfbr , ncelbr ,                            &
   nvar   , nscal  , nphas  ,                                     &
   nideve , nrdeve , nituse , nrtuse ,                            &
   ivar   , iconvp , idiffp , nswrgp , imligp , ircflp ,          &
   ischcp , isstpp , inc    , imrgra , iccocg ,                   &
   ipp    , iwarnp ,                                              &
   blencp , epsrgp , climgp , extrap ,                            &
   ifacel , ifabor , ifmfbr , ifmcel , iprfml ,                   &
   ipnfac , nodfac , ipnfbr , nodfbr ,                            &
   idevel , ituser , ia     ,                                     &
   xyzcen , surfac , surfbo , cdgfac , cdgfbo , xyznod , volume , &
   pvar   , coefap , coefbp , cofafp , cofbfp ,                   &
   flumas , flumab , viscf  , viscb  ,                            &
   flvarf , flvarb ,                                              &
   dpdx   , dpdy   , dpdz   , dpdxa  , dpdya  , dpdza  ,          &
   rdevel , rtuser , ra     )

!===============================================================================
! FONCTION :
! ---------

! CALCUL DU FLUX DE CONVECTION-DIFFUSION D'UNE VARIABLE AUX FACES

!                    .                    ----->        -->
! FLVARF (FACEij) =  m   PVAR  - Visc   ( grad PVAR )  . n
!                     ij     ij      ij             ij    ij

!                  .                 ----->       -->
! FLVARB (FABi) =  m  PVAR - Visc  ( grad PVAR ) . n
!                   i     i      i             i    i


! CALCUL EN UPWIND

!-------------------------------------------------------------------------------
! Arguments
!__________________.____._____.________________________________________________.
! name             !type!mode ! role                                           !
!__________________!____!_____!________________________________________________!
! idbia0           ! i  ! <-- ! number of first free position in ia            !
! idbra0           ! i  ! <-- ! number of first free position in ra            !
! ndim             ! i  ! <-- ! spatial dimension                              !
! ncelet           ! i  ! <-- ! number of extended (real + ghost) cells        !
! ncel             ! i  ! <-- ! number of cells                                !
! nfac             ! i  ! <-- ! number of interior faces                       !
! nfabor           ! i  ! <-- ! number of boundary faces                       !
! nfml             ! i  ! <-- ! number of families (group classes)             !
! nprfml           ! i  ! <-- ! number of properties per family (group class)  !
! nnod             ! i  ! <-- ! number of vertices                             !
! lndfac           ! i  ! <-- ! size of nodfac indexed array                   !
! lndfbr           ! i  ! <-- ! size of nodfbr indexed array                   !
! ncelbr           ! i  ! <-- ! number of cells with faces on boundary         !
! nvar             ! i  ! <-- ! total number of variables                      !
! nscal            ! i  ! <-- ! total number of scalars                        !
! nphas            ! i  ! <-- ! number of phases                               !
! nideve, nrdeve   ! i  ! <-- ! sizes of idevel and rdevel arrays              !
! nituse, nrtuse   ! i  ! <-- ! sizes of ituser and rtuser arrays              !
! ivar             ! e  ! <-- ! numero de la variable                          !
! iconvp           ! e  ! <-- ! indicateur = 1 convection, 0 sinon             !
! idiffp           ! e  ! <-- ! indicateur = 1 diffusion , 0 sinon             !
! nswrgp           ! e  ! <-- ! nombre de sweep pour reconstruction            !
!                  !    !     !             des gradients                      !
! imligp           ! e  ! <-- ! methode de limitation du gradient              !
!                  !    !     !  < 0 pas de limitation                         !
!                  !    !     !  = 0 a partir des gradients voisins            !
!                  !    !     !  = 1 a partir du gradient moyen                !
! ircflp           ! e  ! <-- ! indicateur = 1 rec flux, 0 sinon               !
! ischcp           ! e  ! <-- ! indicateur = 1 centre , 0 2nd order            !
! isstpp           ! e  ! <-- ! indicateur = 1 sans test de pente              !
!                  !    !     !            = 0 avec test de pente              !
! inc              ! e  ! <-- ! indicateur = 0 resol sur increment             !
!                  !    !     !              1 sinon                           !
! imrgra           ! e  ! <-- ! indicateur = 0 gradrc 97                       !
!                  ! e  ! <-- !            = 1 gradmc 99                       !
! iccocg           ! e  ! <-- ! indicateur = 1 pour recalcul de cocg           !
!                  !    !     !              0 sinon                           !
! ipp              ! e  ! <-- ! numero de variable pour post                   !
! iwarnp           ! i  ! <-- ! verbosity                                      !
! blencp           ! r  ! <-- ! 1 - proportion d'upwind                        !
! epsrgp           ! r  ! <-- ! precision relative pour la                     !
!                  !    !     !  reconstruction des gradients 97               !
! climgp           ! r  ! <-- ! coef gradient*distance/ecart                   !
! extrap           ! r  ! <-- ! coef extrap gradient                           !
! ifacel(2, nfac)  ! ia ! <-- ! interior faces -> cells connectivity           !
! ifabor(nfabor)   ! ia ! <-- ! boundary faces -> cells connectivity           !
! ifmfbr(nfabor)   ! ia ! <-- ! boundary face family numbers                   !
! ifmcel(ncelet)   ! ia ! <-- ! cell family numbers                            !
! iprfml           ! ia ! <-- ! property numbers per family                    !
!  (nfml, nprfml)  !    !     !                                                !
! ipnfac(nfac+1)   ! ia ! <-- ! interior faces -> vertices index (optional)    !
! nodfac(lndfac)   ! ia ! <-- ! interior faces -> vertices list (optional)     !
! ipnfbr(nfabor+1) ! ia ! <-- ! boundary faces -> vertices index (optional)    !
! nodfbr(lndfbr)   ! ia ! <-- ! boundary faces -> vertices list (optional)     !
! idevel(nideve)   ! ia ! <-> ! integer work array for temporary development   !
! ituser(nituse)   ! ia ! <-> ! user-reserved integer work array               !
! ia(*)            ! te ! --- ! macro tableau entier                           !
! xyzcen           ! ra ! <-- ! cell centers                                   !
!  (ndim, ncelet)  !    !     !                                                !
! surfac           ! ra ! <-- ! interior faces surface vectors                 !
!  (ndim, nfac)    !    !     !                                                !
! surfbo           ! ra ! <-- ! boundary faces surface vectors                 !
!  (ndim, nfabor)  !    !     !                                                !
! cdgfac           ! ra ! <-- ! interior faces centers of gravity              !
!  (ndim, nfac)    !    !     !                                                !
! cdgfbo           ! ra ! <-- ! boundary faces centers of gravity              !
!  (ndim, nfabor)  !    !     !                                                !
! xyznod           ! ra ! <-- ! vertex coordinates (optional)                  !
!  (ndim, nnod)    !    !     !                                                !
! volume(ncelet)   ! ra ! <-- ! cell volumes                                   !
! pvar (ncelet     ! tr ! <-- ! variable resolue (instant precedent)           !
! coefap, b        ! tr ! <-- ! tableaux des cond lim pour p                   !
!   (nfabor)       !    !     !  sur la normale a la face de bord              !
! cofafp, b        ! tr ! <-- ! tableaux des cond lim pour le flux de          !
!   (nfabor)       !    !     !  diffusion de p                                !
! flumas(nfac)     ! tr ! <-- ! flux de masse aux faces internes               !
! flumab(nfabor    ! tr ! <-- ! flux de masse aux faces de bord                !
! viscf (nfac)     ! tr ! <-- ! visc*surface/dist aux faces internes           !
!                  !    !     !  pour second membre                            !
! viscb (nfabor    ! tr ! <-- ! visc*surface/dist aux faces de bord            !
!                  !    !     !  pour second membre                            !
! flvarf(nfac)     ! tr ! --> ! flux de convection-diffusion                   !
!                  !    !     !  aux faces internes                            !
! flvarb(nfabor    ! tr ! --> ! flux de convection-diffusion                   !
!                  !    !     !  aux faces de bord                             !
! dpdx,y,z         ! tr ! --- ! tableau de travail pour le grad de p           !
!    (ncelet)      !    !     !                                                !
! dpdxa,ya,za      ! tr ! --- ! tableau de travail pour le grad de p           !
!    (ncelet)      !    !     !  avec decentrement amont                       !
! rdevel(nrdeve)   ! ra ! <-> ! real work array for temporary development      !
! rtuser(nrtuse)   ! ra ! <-> ! user-reserved real work array                  !
! ra(*)            ! ra ! --- ! main real work array                           !
!__________________!____!_____!________________________________________________!

!     TYPE : E (ENTIER), R (REEL), A (ALPHANUMERIQUE), T (TABLEAU)
!            L (LOGIQUE)   .. ET TYPES COMPOSES (EX : TR TABLEAU REEL)
!     MODE : <-- donnee, --> resultat, <-> Donnee modifiee
!            --- tableau de travail
!===============================================================================

implicit none

!===============================================================================
! Common blocks
!===============================================================================

include "paramx.h"
include "pointe.h"
include "vector.h"
include "entsor.h"
include "period.h"
include "parall.h"

!===============================================================================

! Arguments

integer          idbia0 , idbra0
integer          ndim   , ncelet , ncel   , nfac   , nfabor
integer          nfml   , nprfml
integer          nnod   , lndfac , lndfbr , ncelbr
integer          nvar   , nscal  , nphas
integer          nideve , nrdeve , nituse , nrtuse
integer          ivar   , iconvp , idiffp , nswrgp , imligp
integer          ircflp , ischcp , isstpp
integer          inc    , imrgra , iccocg
integer          iwarnp , ipp
double precision blencp , epsrgp , climgp, extrap

integer          ifacel(2,nfac) , ifabor(nfabor)
integer          ifmfbr(nfabor) , ifmcel(ncelet)
integer          iprfml(nfml,nprfml)
integer          ipnfac(nfac+1), nodfac(lndfac)
integer          ipnfbr(nfabor+1), nodfbr(lndfbr)
integer          idevel(nideve), ituser(nituse)
integer          ia(*)

double precision xyzcen(ndim,ncelet)
double precision surfac(ndim,nfac), surfbo(ndim,nfabor)
double precision cdgfac(ndim,nfac), cdgfbo(ndim,nfabor)
double precision xyznod(ndim,nnod), volume(ncelet)
double precision pvar (ncelet), coefap(nfabor), coefbp(nfabor)
double precision                cofafp(nfabor), cofbfp(nfabor)
double precision flumas(nfac), flumab(nfabor)
double precision viscf (nfac), viscb (nfabor)
double precision flvarf(nfac), flvarb(nfabor)
double precision dpdx (ncelet),dpdy (ncelet),dpdz (ncelet)
double precision dpdxa(ncelet),dpdya(ncelet),dpdza(ncelet)
double precision rdevel(nrdeve), rtuser(nrtuse), ra(*)

! Local variables

character*80     chaine
character*8      cnom
integer          idebia, idebra
integer          ifac,ii,jj,infac,iel, iij, iii
integer          iphydp
double precision pfac,pfacd,pip,pjp,flui,fluj,flux
double precision pif,pjf
double precision dpxf,dpyf,dpzf
double precision dijpfx, dijpfy, dijpfz
double precision diipfx, diipfy, diipfz
double precision djjpfx, djjpfy, djjpfz
double precision diipbx, diipby, diipbz
double precision pond

!===============================================================================

!===============================================================================
! 1.  INITIALISATION
!===============================================================================

idebia = idbia0
idebra = idbra0

chaine = nomvar(ipp)
cnom   = chaine(1:8)


!===============================================================================
! 2.  CALCUL DU BILAN AVEC TECHNIQUE DE RECONSTRUCTION
!===============================================================================

! ======================================================================
! ---> CALCUL DU GRADIENT DE PVAR
! ======================================================================
!    DPDX sert pour la reconstruction des flux de diffusion
!       (convection en upwind)
!    On doit donc le calculer uniquement si on a de la diffusion
!       et qu'on reconstruit les flux

if( idiffp.ne.0 .and. ircflp.eq.1 ) then

  iphydp = 0
  call grdcel                                                     &
  !==========
 ( idebia , idebra ,                                              &
   ndim   , ncelet , ncel   , nfac   , nfabor , nfml   , nprfml , &
   nnod   , lndfac , lndfbr , ncelbr , nphas  ,                   &
   nideve , nrdeve , nituse , nrtuse ,                            &
   ivar   , imrgra , inc    , iccocg , nswrgp , imligp ,  iphydp ,&
   iwarnp , nfecra , epsrgp , climgp , extrap ,                   &
   ifacel , ifabor , ifmfbr , ifmcel , iprfml ,                   &
   ipnfac , nodfac , ipnfbr , nodfbr ,                            &
   idevel , ituser , ia     ,                                     &
   xyzcen , surfac , surfbo , cdgfac , cdgfbo , xyznod , volume , &
   dpdxa  , dpdxa  , dpdxa  ,                                     &
   pvar   , coefap , coefbp ,                                     &
   dpdx   , dpdy   , dpdz   ,                                     &
!        ------   ------   ------
   dpdxa  , dpdya  , dpdza  ,                                     &
   rdevel , rtuser , ra     )

else
  do iel = 1, ncelet
    dpdx(iel) = 0.d0
    dpdy(iel) = 0.d0
    dpdz(iel) = 0.d0
  enddo
endif


! ======================================================================
! ---> ASSEMBLAGE A PARTIR DES FACETTES FLUIDES
! ======================================================================

infac = 0

do ifac = 1, nfac
  flvarf(ifac) = 0.d0
enddo

do ifac = 1, nfabor
  flvarb(ifac) = 0.d0
enddo


!  --> FLUX UPWIND PUR
!  =====================

if (ivecti.eq.1) then

!CDIR NODEP
  do ifac = 1, nfac

    ii = ifacel(1,ifac)
    jj = ifacel(2,ifac)

    iij = idijpf-1+3*(ifac-1)
    dijpfx = ra(iij+1)
    dijpfy = ra(iij+2)
    dijpfz = ra(iij+3)

    pond   = ra(ipond-1+ifac)

! ON RECALCULE A CE NIVEAU II' ET JJ'

    diipfx = cdgfac(1,ifac) - (xyzcen(1,ii)+                      &
               (1.d0-pond) * dijpfx)
    diipfy = cdgfac(2,ifac) - (xyzcen(2,ii)+                      &
               (1.d0-pond) * dijpfy)
    diipfz = cdgfac(3,ifac) - (xyzcen(3,ii)+                      &
               (1.d0-pond) * dijpfz)
    djjpfx = cdgfac(1,ifac) -  xyzcen(1,jj)+                      &
                   pond  * dijpfx
    djjpfy = cdgfac(2,ifac) -  xyzcen(2,jj)+                      &
                   pond  * dijpfy
    djjpfz = cdgfac(3,ifac) -  xyzcen(3,jj)+                      &
                   pond  * dijpfz

    dpxf = 0.5d0*(dpdx(ii) + dpdx(jj))
    dpyf = 0.5d0*(dpdy(ii) + dpdy(jj))
    dpzf = 0.5d0*(dpdz(ii) + dpdz(jj))

!     reconstruction uniquement si IRCFLP = 1
    pip = pvar(ii)                                                &
           + ircflp*(dpxf*diipfx+dpyf*diipfy+dpzf*diipfz)
    pjp = pvar(jj)                                                &
           + ircflp*(dpxf*djjpfx+dpyf*djjpfy+dpzf*djjpfz)

    flui = 0.5d0*( flumas(ifac) +abs(flumas(ifac)) )
    fluj = 0.5d0*( flumas(ifac) -abs(flumas(ifac)) )

    pif = pvar(ii)
    pjf = pvar(jj)
    infac = infac+1

    flux = iconvp*( flui*pif +fluj*pjf )                          &
           + idiffp*viscf(ifac)*( pip -pjp )

! --- FLVARF(IFAC) : flux de convection-diffusion de la variable
!                    a la face ij

    flvarf(ifac) = flux

  enddo

else

! VECTORISATION NON FORCEE
  do ifac = 1, nfac

    ii = ifacel(1,ifac)
    jj = ifacel(2,ifac)

    iij = idijpf-1+3*(ifac-1)
    dijpfx = ra(iij+1)
    dijpfy = ra(iij+2)
    dijpfz = ra(iij+3)

    pond   = ra(ipond-1+ifac)

! ON RECALCULE A CE NIVEAU II' ET JJ'

    diipfx = cdgfac(1,ifac) - (xyzcen(1,ii)+                      &
               (1.d0-pond) * dijpfx)
    diipfy = cdgfac(2,ifac) - (xyzcen(2,ii)+                      &
               (1.d0-pond) * dijpfy)
    diipfz = cdgfac(3,ifac) - (xyzcen(3,ii)+                      &
               (1.d0-pond) * dijpfz)
    djjpfx = cdgfac(1,ifac) -  xyzcen(1,jj)+                      &
                   pond  * dijpfx
    djjpfy = cdgfac(2,ifac) -  xyzcen(2,jj)+                      &
                   pond  * dijpfy
    djjpfz = cdgfac(3,ifac) -  xyzcen(3,jj)+                      &
                   pond  * dijpfz

    dpxf = 0.5d0*(dpdx(ii) + dpdx(jj))
    dpyf = 0.5d0*(dpdy(ii) + dpdy(jj))
    dpzf = 0.5d0*(dpdz(ii) + dpdz(jj))

    pip = pvar(ii)                                                &
           + ircflp*(dpxf*diipfx+dpyf*diipfy+dpzf*diipfz)
    pjp = pvar(jj)                                                &
           + ircflp*(dpxf*djjpfx+dpyf*djjpfy+dpzf*djjpfz)

    flui = 0.5d0*( flumas(ifac) +abs(flumas(ifac)) )
    fluj = 0.5d0*( flumas(ifac) -abs(flumas(ifac)) )

    pif = pvar(ii)
    pjf = pvar(jj)
    infac = infac+1

    flux = iconvp*( flui*pif +fluj*pjf )                          &
           + idiffp*viscf(ifac)*( pip -pjp )

! --- FLVARF(IFAC) : flux de convection-diffusion de la variable
!                    a la face ij

    flvarf(ifac) = flux

  enddo

endif


! ======================================================================
! ---> ASSEMBLAGE A PARTIR DES FACETTES DE BORD
! ======================================================================

if (ivectb.eq.1) then

!CDIR NODEP
  do ifac = 1, nfabor

    ii = ifabor(ifac)

    iii = idiipb-1+3*(ifac-1)
    diipbx = ra(iii+1)
    diipby = ra(iii+2)
    diipbz = ra(iii+3)

    flui = 0.5d0*( flumab(ifac) +abs(flumab(ifac)) )
    fluj = 0.5d0*( flumab(ifac) -abs(flumab(ifac)) )

    pip = pvar(ii)                                                &
       +ircflp*(dpdx(ii)*diipbx+dpdy(ii)*diipby+dpdz(ii)*diipbz)

    pfac  = inc*coefap(ifac) +coefbp(ifac)*pip
    pfacd = inc*cofafp(ifac) +cofbfp(ifac)*pip

    flux = iconvp*( flui*pvar(ii) +fluj*pfac )                    &
         + idiffp*viscb(ifac)*( pip -pfacd )

! --- FLVARB(IFAC) : flux de convection-diffusion de la variable
!                    a la face de bord i

    flvarb(ifac) = flux

  enddo

else

  do ifac = 1, nfabor

    ii = ifabor(ifac)

    iii = idiipb-1+3*(ifac-1)
    diipbx = ra(iii+1)
    diipby = ra(iii+2)
    diipbz = ra(iii+3)

    flui = 0.5d0*( flumab(ifac) +abs(flumab(ifac)) )
    fluj = 0.5d0*( flumab(ifac) -abs(flumab(ifac)) )

    pip = pvar(ii)                                                &
       +ircflp*(dpdx(ii)*diipbx+dpdy(ii)*diipby+dpdz(ii)*diipbz)

    pfac  = inc*coefap(ifac) +coefbp(ifac)*pip
    pfacd = inc*cofafp(ifac) +cofbfp(ifac)*pip

    flux = iconvp*( flui*pvar(ii) +fluj*pfac )                    &
         + idiffp*viscb(ifac)*( pip -pfacd )

! --- FLVARB(IFAC) : flux de convection-diffusion de la variable
!                    a la face de bord i

    flvarb(ifac) = flux

  enddo

endif

!--------
! FORMATS
!--------


!----
! FIN
!----

return

end subroutine
