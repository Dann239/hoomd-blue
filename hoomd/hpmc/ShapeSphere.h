// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

#pragma once

#include "hoomd/HOOMDMath.h"
#include "hoomd/BoxDim.h"
#include "HPMCPrecisionSetup.h"
#include "hoomd/VectorMath.h"
#include "Moves.h"
#include "hoomd/AABB.h"
#include "hoomd/hpmc/OBB.h"
#include "hoomd/hpmc/HPMCMiscFunctions.h"
#include <sstream>

#include <stdexcept>

#ifdef __HIPCC__
#define DEVICE __device__
#define HOSTDEVICE __host__ __device__
#else
#define DEVICE
#define HOSTDEVICE
#include <pybind11/pybind11.h>
#endif

#define SMALL 1e-5

namespace hpmc
{

/** HPMC shape parameter base class

    HPMC shape parameters must be aligned on 32-byte boundaries for AVX acceleration. The ShapeParam
    base class implements the necessary aligned memory allocation operations. It also provides
    empty load_shared and allocated_shared implementations which enabled caching deep copied managed
    data arrays in shared memory.

    TODO Move base methods out into their own file. ShapeSphere.h will then no longer need to be
          included by everything.
*/
struct ShapeParams
    {
    /// Custom new operator
    static void* operator new(std::size_t sz)
        {
        void *ret = 0;
        int retval = posix_memalign(&ret, 32, sz);
        if (retval != 0)
            {
            throw std::runtime_error("Error allocating aligned memory");
            }

        return ret;
        }

    /// Custom new operator for arrays
    static void* operator new[](std::size_t sz)
        {
        void *ret = 0;
        int retval = posix_memalign(&ret, 32, sz);
        if (retval != 0)
            {
            throw std::runtime_error("Error allocating aligned memory");
            }

        return ret;
        }

    /// Custom delete operator
    static void operator delete(void *ptr)
        {
        free(ptr);
        }

    /// Custom delete operator for arrays
    static void operator delete[](void *ptr)
        {
        free(ptr);
        }

    /** Load dynamic data members into shared memory and increase pointer

        @param ptr Pointer to load data to (will be incremented)
        @param available_bytes Size of remaining shared memory allocation
     */
    DEVICE void load_shared(char *& ptr, unsigned int &available_bytes)
        {
        // default implementation does nothing
        }

    /** Determine size of the shared memory allocation

        @param ptr Pointer to increment
        @param available_bytes Size of remaining shared memory allocation
     */
    HOSTDEVICE void allocate_shared(char *& ptr, unsigned int &available_bytes) const
        {
        // default implementation does nothing
        }

    };


/** Parameters that define a sphere shape

    Spheres in HPMC are defined by their radius. Spheres may or may not be orientable. The
    orientation of a sphere does not enter into the overlap check, but the particle's orientation
    may be used by other code paths (e.g. the patch potential).
*/
struct SphereParams : ShapeParams
    {
    /// The radius of the sphere
    OverlapReal radius;

    /// True when move statistics should not be counted
    bool ignore;

    /// True when the shape may be oriented
    bool isOriented;

    #ifdef ENABLE_HIP
    /// Set CUDA memory hints
    void set_memory_hint() const
        {
        }
    #endif

    #ifndef __HIPCC__

    /// Default constructor
    SphereParams() { }

    /// Construct from a Python dictionary
    SphereParams(pybind11::dict v, bool managed=false)
        {
        ignore = v["ignore_statistics"].cast<bool>();
        radius = v["diameter"].cast<OverlapReal>() / OverlapReal(2.0);
        isOriented = v["orientable"].cast<bool>();
        }

    /// Convert parameters to a python dictionary
    pybind11::dict asDict()
        {
        pybind11::dict v;
        v["diameter"] =  radius * OverlapReal(2.0);
        v["orientable"] = isOriented;
        v["ignore_statistics"] = ignore;
        return v;
        }

    #endif
    } __attribute__((aligned(32)));

/** Sphere shape

    Shape classes define the interface used by IntegratorHPMCMono, ComputeFreeVolume, and other
    classes to check for overlaps between shapes, find their extend in space, and other operations.
    These classes are specified via template parameters to these classes so that the compiler may
    fully inline all uses of the shape API.

    ShapeSphere defines this API for spheres.

    Some portions of the API (e.g. test_overlap) are implemented as specialized function templates.

    TODO Should we remove orientation as a member variable from the shape API. It should be passed
          when needed.
    TODO Don't use specialized templates for things that should be methods (i.e. a.overlapsWith(b))
    TODO add hpmc::shape namespace
*/
struct ShapeSphere
    {
    /// Define the parameter type
    typedef SphereParams param_type;

    /// Construct a shape at a given orientation
    DEVICE ShapeSphere(const quat<Scalar>& _orientation, const param_type& _params)
        : orientation(_orientation), params(_params) {}

    /// Check if the shape may be rotated
    DEVICE bool hasOrientation() const
        {
        return params.isOriented;
        }

    /// Check if this shape should be ignored in the move statistics
    DEVICE bool ignoreStatistics() const { return params.ignore; }

    /// Get the circumsphere diameter of the shape
    DEVICE OverlapReal getCircumsphereDiameter() const
        {
        return params.radius*OverlapReal(2.0);
        }

    /// Get the in-sphere radius of the shape
    DEVICE OverlapReal getInsphereRadius() const
        {
        return params.radius;
        }

    /// Return the bounding box of the shape in world coordinates
    DEVICE detail::AABB getAABB(const vec3<Scalar>& pos) const
        {
        return detail::AABB(pos, params.radius);
        }

    /// Return a tight fitting OBB around the shape
    DEVICE detail::OBB getOBB(const vec3<Scalar>& pos) const
        {
        // just use the AABB for now
        return detail::OBB(getAABB(pos));
        }

    /// Returns true if this shape splits the overlap check over several threads of a warp using threadIdx.x
    HOSTDEVICE static bool isParallel() { return false; }

    /// Returns true if the overlap check supports sweeping both shapes by a sphere of given radius
    HOSTDEVICE static bool supportsSweepRadius()
        {
        return true;
        }

    /// Orientation of the sphere
    quat<Scalar> orientation;

    /// Sphere parameters
    const SphereParams &params;
    };

namespace detail
{

//! Test for a common point in the intersection of three spheres
/*! \param Ra radius of first sphere
    \param Rb radius of second sphere
    \param Rc radius of third sphere
    \param ab_t Position of second sphere relative to first
    \param ac_t Position of third sphere relative to first
*/
DEVICE inline bool check_three_spheres_overlap(OverlapReal Ra, OverlapReal Rb, OverlapReal Rc,
    const vec3<Scalar>& ab_t, const vec3<Scalar>& ac_t)
    {
    vec3<OverlapReal> r_ab(ab_t);
    vec3<OverlapReal> r_ac(ac_t);
    vec3<OverlapReal> r_bc = r_ac-r_ab;
    OverlapReal rab_sq = dot(r_ab,r_ab);
    OverlapReal rab = fast::sqrt(rab_sq);
    OverlapReal rac_sq = dot(r_ac,r_ac);
    OverlapReal rac = fast::sqrt(rac_sq);
    OverlapReal rbc_sq = dot(r_bc,r_bc);
    OverlapReal rbc = fast::sqrt(rbc_sq);

    // first check trivial cases where one sphere is contained in the other
    if (rab + Rb <= Ra)
        {
        // b is in a
        return rbc_sq <= (Rb + Rc)*(Rb + Rc);
        }
    else if (rab + Ra <= Rb)
        {
        // a is in b
        return rac_sq <= (Ra + Rc)*(Ra + Rc);
        }

    if (rac + Rc <= Ra)
        {
        // c is in a
        return rbc_sq <= (Rb + Rc)*(Rb + Rc);
        }
    else if (rac + Ra <= Rc)
        {
        // a is in c
        return rab_sq <= (Ra + Rb)*(Ra + Rb);
        }

    if (rbc + Rc <= Rb)
        {
        // c is in b
        return rac_sq <= (Ra + Rc)*(Ra + Rc);
        }
    else if (rbc + Rb <= Rc)
        {
        // b is in c
        return rab_sq <= (Ra + Rb)*(Ra + Rb);
        }

    // no volume is entirely contained in the other, surfaces either intersect or don't

    // https://gamedev.stackexchange.com/questions/75756/sphere-sphere-intersection-and-circle-sphere-intersection
    // do a and b intersect in a circle?
    if (rab_sq <= (Ra + Rb)*(Ra + Rb))
        {
        // center of intersection circle
        vec3<OverlapReal> c_c = OverlapReal(0.5)*(rab_sq-Rb*Rb+Ra*Ra)/rab_sq*r_ab;

        // check for circle-sphere intersection

        vec3<OverlapReal> n = r_ab*fast::rsqrt(dot(r_ab,r_ab));
        OverlapReal d = dot(n,c_c-r_ac);

        if (d*d > Rc*Rc)
            // c does not intersect plane of intersection circle
            return false;

        // center and radius of circle on c
        vec3<OverlapReal> c_p = r_ac + d*n;
        OverlapReal r_p = fast::sqrt(Rc*Rc - d*d);

        // radius of intersection circle
        OverlapReal r_c=OverlapReal(0.5)*fast::sqrt((OverlapReal(4.0)*rab_sq*Ra*Ra-(rab_sq-Rb*Rb+Ra*Ra)*(rab_sq-Rb*Rb+Ra*Ra))/rab_sq);

        // test overlap of circles
        return dot(c_p-c_c,c_p-c_c) <= (r_c+r_p)*(r_c+r_p);
        }

    // no intersection
    return false;
    }
} // end namespace detail

//! Check if circumspheres overlap
/*! \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \returns true if the circumspheres of both shapes overlap

    \ingroup shape
*/
template<class ShapeA, class ShapeB>
DEVICE inline bool check_circumsphere_overlap(const vec3<Scalar>& r_ab, const ShapeA& a, const ShapeB &b,
    const OverlapReal sweep_radius_a = OverlapReal(0.0), const OverlapReal sweep_radius_b = OverlapReal(0.0))
    {
    vec2<OverlapReal> dr(OverlapReal(r_ab.x), OverlapReal(r_ab.y));

    OverlapReal rsq = dot(dr,dr);
    OverlapReal DaDb = a.getCircumsphereDiameter() + b.getCircumsphereDiameter()
        + OverlapReal(2.0)*(sweep_radius_a + sweep_radius_b);
    return (rsq*OverlapReal(4.0) <= DaDb * DaDb);
    }

//! Check if three circumspheres overlap in a common point
/*! \param a first shape
    \param b second shape
    \param c third shape
    \param ab_t Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param ac_t Vector defining the position of shape c relative to shape a (r_c - r_a)
    \param sweep_radius_a Additional radius to sweep shape a by
    \param sweep_radius_b Additional radius to sweep shape b by
    \param sweep_radius_c Additional radius to sweep shape c by
    \returns true if the circumspheres of both shapes overlap

    \ingroup shape
*/
template<class ShapeA, class ShapeB, class ShapeC>
DEVICE inline bool check_circumsphere_overlap_three(const ShapeA& a, const ShapeB& b, const ShapeC &c,
    const vec3<OverlapReal>& ab_t, const vec3<OverlapReal>& ac_t,
    OverlapReal sweep_radius_a=OverlapReal(0.0), OverlapReal sweep_radius_b=OverlapReal(0.0),
    OverlapReal sweep_radius_c=OverlapReal(0.0))
    {
    // Default implementation
    OverlapReal Ra = OverlapReal(0.5)*a.getCircumsphereDiameter() + sweep_radius_a;
    OverlapReal Rb = OverlapReal(0.5)*b.getCircumsphereDiameter() + sweep_radius_b;
    OverlapReal Rc = OverlapReal(0.5)*c.getCircumsphereDiameter() + sweep_radius_c;

    return detail::check_three_spheres_overlap(Ra,Rb,Rc,ab_t,ac_t);
    }

//! Check if bounding volumes (OBBs) overlap (generic template)
/*! \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \returns true if the circumspheres of both shapes overlap

    \ingroup shape
*/
template<class ShapeA, class ShapeB>
DEVICE inline bool check_obb_overlap(const vec3<Scalar>& r_ab, const ShapeA& a, const ShapeB &b)
    {
    // check overlap between OBBs
    return detail::overlap(a.getOBB(vec3<Scalar>(0,0,0)), b.getOBB(r_ab));
    }

//! Check if bounding volumes (OBBs) of two spheres overlap
/*! \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \returns true if the circumspheres of both shapes overlap

    \ingroup shape
*/
DEVICE inline bool check_obb_overlap(const vec3<Scalar>& r_ab, const ShapeSphere& a,
    const ShapeSphere &b)
    {
    // for now, always return true
    return true;
    }


//! Check if three circumspheres overlap in a common point
/*! \param a first shape
    \param b second shape
    \param c third shape
    \param ab_t Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param ac_t Vector defining the position of shape c relative to shape a (r_c - r_a)
    \param sweep_radius_a additional sweep radius
    \param sweep_radius_b additional sweep radius
    \param sweep_radius_c additional sweep radius
    \returns true if the circumspheres of both shapes overlap

    \ingroup shape
*/
template<>
DEVICE inline bool check_circumsphere_overlap_three(const ShapeSphere& a, const ShapeSphere& b, const ShapeSphere &c,
    const vec3<OverlapReal>& ab_t, const vec3<OverlapReal>& ac_t, OverlapReal sweep_radius_a, OverlapReal sweep_radius_b,
    OverlapReal sweep_radius_c)
    {
    // for spheres, always return true
    return true;
    }


//! Define the general overlap function
/*! This is just a convenient spot to put this to make sure it is defined early
    \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \param err Incremented if there is an error condition. Left unchanged otherwise.
    \param sweep_radius_a Additional radius to sweep both shapes by
    \param sweep_radius_b Additional radius to sweep both shapes by
    \returns true when *a* and *b* overlap, and false when they are disjoint
*/
template <class ShapeA, class ShapeB>
DEVICE inline bool test_overlap(const vec3<Scalar>& r_ab, const ShapeA &a, const ShapeB& b, unsigned int& err,
    Scalar sweep_radius_a=Scalar(0.0), Scalar sweep_radius_b=Scalar(0.0))
    {
    // default implementation returns true, will make it obvious if something calls this
    return true;
    }

//! Sphere-Sphere overlap
/*! \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \param err in/out variable incremented when error conditions occur in the overlap test
    \param sweep_radius_a Additional radius to sweep the first shape by
    \param sweep_radius_b Additional radius to sweep the second shape by
    \returns true when *a* and *b* overlap, and false when they are disjoint

    \ingroup shape
*/
template <>
DEVICE inline bool test_overlap<ShapeSphere, ShapeSphere>(const vec3<Scalar>& r_ab, const ShapeSphere& a, const ShapeSphere& b, unsigned int& err,
    Scalar sweep_radius_a, Scalar sweep_radius_b)
    {
    vec3<OverlapReal> dr(r_ab);

    OverlapReal rsq = dot(dr,dr);

    OverlapReal RaRb = a.params.radius + b.params.radius + OverlapReal(sweep_radius_a + sweep_radius_b);
    if (rsq < RaRb*RaRb)
        {
        return true;
        }
    else
        {
        return false;
        }
    }

//! Test for overlap of a third particle with the intersection of two shapes
/*! \param a First shape to test
    \param b Second shape to test
    \param c Third shape to test
    \param ab_t Position of second shape relative to first
    \param ac_t Position of third shape relative to first
    \param err Output variable that is incremented upon non-convergence
    \param sweep_radius_a Radius of a sphere to sweep the first shape py
    \param sweep_radius_b Radius of a sphere to sweep the second shape by
    \param sweep_radius_c Radius of a sphere to sweep the third shape by
*/
template <class ShapeA, class ShapeB, class ShapeC>
DEVICE inline bool test_overlap_intersection(const ShapeA& a, const ShapeB& b, const ShapeC& c,
    const vec3<Scalar>& ab_t, const vec3<Scalar>& ac_t, unsigned int &err,
    Scalar sweep_radius_a = Scalar(0.0), Scalar sweep_radius_b = Scalar(0.0),
    Scalar sweep_radius_c = Scalar(0.0))
    {
    // default returns true, so it is obvious if something calls this
    return true;
    }

//! Test for a common point in the intersection of three spheres
/*! \param a First shape to test
    \param b Second shape to test
    \param c Third shape to test
    \param ab_t Position of second shape relative to first
    \param ac_t Position of third shape relative to first
    \param err Output variable that is incremented upon non-convergence
    \param sweep_radius_a Radius of a sphere to sweep the first sphere by
    \param sweep_radius_b Radius of a sphere to sweep the second sphere by
    \param sweep_radius_c Radius of a sphere to sweep the third sphere by
*/
template<>
DEVICE inline bool test_overlap_intersection(const ShapeSphere& a, const ShapeSphere& b, const ShapeSphere& c,
    const vec3<Scalar>& ab_t, const vec3<Scalar>& ac_t, unsigned int &err,
    Scalar sweep_radius_a, Scalar sweep_radius_b, Scalar sweep_radius_c)
    {
    OverlapReal Ra = a.params.radius + OverlapReal(sweep_radius_a);
    OverlapReal Rb = b.params.radius + OverlapReal(sweep_radius_b);
    OverlapReal Rc = c.params.radius + OverlapReal(sweep_radius_c);

    return detail::check_three_spheres_overlap(Ra,Rb,Rc,ab_t,ac_t);
    }

#ifndef __HIPCC__
template<class Shape>
std::string getShapeSpec(const Shape& shape)
    {
    // default implementation
    throw std::runtime_error("Shape definition not supported for this shape class.");
    }

template<>
inline std::string getShapeSpec(const ShapeSphere& sphere)
    {
    std::ostringstream shapedef;
    shapedef << "{\"type\": \"Sphere\", \"diameter\": " << sphere.params.radius*OverlapReal(2.0) << "}";
    return shapedef.str();
    }
#endif


}; // end namespace hpmc

#undef DEVICE
#undef HOSTDEVICE
