// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Include the defined classes that are to be exported to python
#include "ComputeFreeVolume.h"
#include "IntegratorHPMC.h"
#include "IntegratorHPMCMono.h"

#include "ComputeSDF.h"
#include "ShapeFacetedEllipsoid.h"

#include "ExternalCallback.h"
#include "ExternalField.h"
#include "ExternalFieldComposite.h"
#include "ExternalFieldLattice.h"
#include "ExternalFieldWall.h"

#include "UpdaterClusters.h"
#include "UpdaterExternalFieldWall.h"
#include "UpdaterMuVT.h"

#ifdef ENABLE_HIP
#include "ComputeFreeVolumeGPU.h"
#include "IntegratorHPMCMonoGPU.h"
#include "UpdaterClustersGPU.h"
#endif

namespace hoomd
    {
namespace hpmc
    {
namespace detail
    {
//! Export the base HPMCMono integrators
void export_faceted_ellipsoid(pybind11::module& m)
    {
    export_IntegratorHPMCMono<ShapeFacetedEllipsoid>(m, "IntegratorHPMCMonoFacetedEllipsoid");
    export_ComputeFreeVolume<ShapeFacetedEllipsoid>(m, "ComputeFreeVolumeFacetedEllipsoid");
    export_ComputeSDF<ShapeFacetedEllipsoid>(m, "ComputeSDFFacetedEllipsoid");
    export_UpdaterMuVT<ShapeFacetedEllipsoid>(m, "UpdaterMuVTFacetedEllipsoid");
    export_UpdaterClusters<ShapeFacetedEllipsoid>(m, "UpdaterClustersFacetedEllipsoid");

    export_ExternalFieldInterface<ShapeFacetedEllipsoid>(m, "ExternalFieldFacetedEllipsoid");
    export_LatticeField<ShapeFacetedEllipsoid>(m, "ExternalFieldLatticeFacetedEllipsoid");
    export_ExternalFieldComposite<ShapeFacetedEllipsoid>(m,
                                                         "ExternalFieldCompositeFacetedEllipsoid");
    export_ExternalFieldWall<ShapeFacetedEllipsoid>(m, "WallFacetedEllipsoid");
    export_UpdaterExternalFieldWall<ShapeFacetedEllipsoid>(
        m,
        "UpdaterExternalFieldWallFacetedEllipsoid");
    export_ExternalCallback<ShapeFacetedEllipsoid>(m, "ExternalCallbackFacetedEllipsoid");

#ifdef ENABLE_HIP
    export_IntegratorHPMCMonoGPU<ShapeFacetedEllipsoid>(m, "IntegratorHPMCMonoFacetedEllipsoidGPU");
    export_ComputeFreeVolumeGPU<ShapeFacetedEllipsoid>(m, "ComputeFreeVolumeFacetedEllipsoidGPU");
    export_UpdaterClustersGPU<ShapeFacetedEllipsoid>(m, "UpdaterClustersFacetedEllipsoidGPU");
#endif
    }

    } // namespace detail
    } // namespace hpmc
    } // namespace hoomd
