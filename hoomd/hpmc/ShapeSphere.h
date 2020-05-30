// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

#include "hoomd/HOOMDMath.h"
#include "hoomd/BoxDim.h"
#include "HPMCPrecisionSetup.h"
#include "hoomd/VectorMath.h"
#include "Moves.h"
#include "hoomd/AABB.h"
#include "hoomd/hpmc/OBB.h"
#include "hoomd/hpmc/HPMCMiscFunctions.h"

#include "Moves.h"

#include <sstream>

#include <stdexcept>

#ifndef __SHAPE_SPHERE_H__
#define __SHAPE_SPHERE_H__

/*! \file ShapeSphere.h
    \brief Defines the sphere shape
*/

// need to declare these class methods with __device__ qualifiers when building in nvcc
// DEVICE is __device__ when included in nvcc and blank when included into the host compiler
#ifdef __HIPCC__
#define DEVICE __device__
#define HOSTDEVICE __host__ __device__
#else
#define DEVICE
#define HOSTDEVICE
#endif

#define SMALL 1e-5

namespace hpmc
{

//! Base class for parameter structure data types
struct param_base
    {
    //! Custom new operator
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

    //! Custom new operator for arrays
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

    //! Custom delete operator
    static void operator delete(void *ptr)
        {
        free(ptr);
        }

    //! Custom delete operator for arrays
    static void operator delete[](void *ptr)
        {
        free(ptr);
        }

    //! Load dynamic data members into shared memory and increase pointer
    /*! \param ptr Pointer to load data to (will be incremented)
        \param available_bytes Size of remaining shared memory allocation
        \param mask bitmask to indicate which arrays we should load
     */
    HOSTDEVICE inline void load_shared(char *& ptr,unsigned int &available_bytes,
                                       unsigned int mask) const
        {
        // default implementation does nothing
        }

    //! Returns the number of available bits for tuning
    HOSTDEVICE static inline unsigned int getTuningBits()
        {
        return 0;
        }
    };


//! Sphere shape template
/*! ShapeSphere implements IntegratorHPMC's shape protocol. It serves at the simplest example of a shape for HPMC

    The parameter defining a sphere is just a single Scalar, the sphere radius.

    \ingroup shape
*/
struct sph_params : param_base
    {
    OverlapReal radius;                 //!< radius of sphere
    unsigned int ignore;                //!< Bitwise ignore flag for stats, overlaps. 1 will ignore, 0 will not ignore
                                        //   First bit is ignore overlaps, Second bit is ignore statistics
    bool isOriented;                    //!< Flag to specify whether a sphere has orientation or not. Intended for
                                        //!  for use with anisotropic/patchy pair potentials.

    #ifdef ENABLE_HIP
    //! Set CUDA memory hints
    void set_memory_hint() const
        {
        // default implementation does nothing
        }
    #endif
    } __attribute__((aligned(32)));

struct ShapeSphere
    {
    //! Define the parameter type
    typedef sph_params param_type;

    //! Temporary storage for depletant insertion
    typedef struct {} depletion_storage_type;

    //! Initialize a shape at a given position
    DEVICE ShapeSphere(const quat<Scalar>& _orientation, const param_type& _params)
        : orientation(_orientation), params(_params) {}

    //! Does this shape have an orientation
    DEVICE inline bool hasOrientation() const
        {
        return params.isOriented;
        }

    //! Ignore flag for acceptance statistics
    DEVICE inline bool ignoreStatistics() const { return params.ignore; }

    //! Get the circumsphere diameter
    DEVICE inline OverlapReal getCircumsphereDiameter() const
        {
        return params.radius*OverlapReal(2.0);
        }

    //! Get the in-sphere radius
    DEVICE OverlapReal getInsphereRadius() const
        {
        return params.radius;
        }

    //! Return the bounding box of the shape in world coordinates
    DEVICE inline detail::AABB getAABB(const vec3<Scalar>& pos) const
        {
        return detail::AABB(pos, params.radius);
        }

    //! Return a tight fitting OBB
    DEVICE inline detail::OBB getOBB(const vec3<Scalar>& pos) const
        {
        return detail::OBB(pos, params.radius);
        }

    //! Returns true if this shape splits the overlap check over several threads of a warp using threadIdx.x
    HOSTDEVICE static bool isParallel() { return false; }

    //! Returns the number of tuning bits for the GPU kernels
    HOSTDEVICE static inline unsigned int getTuningBits()
        {
        return sph_params::getTuningBits();
        }

    quat<Scalar> orientation;    //!< Orientation of the sphere (unused)

    const sph_params &params;        //!< Sphere and ignore flags
    };

//! Check if circumspheres overlap
/*! \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \returns true if the circumspheres of both shapes overlap

    \ingroup shape
*/
template<class ShapeA, class ShapeB>
DEVICE inline bool check_circumsphere_overlap(const vec3<Scalar>& r_ab, const ShapeA& a, const ShapeB &b)
    {
    vec2<OverlapReal> dr(r_ab.x, r_ab.y);

    OverlapReal rsq = dot(dr,dr);
    OverlapReal DaDb = a.getCircumsphereDiameter() + b.getCircumsphereDiameter();
    return (rsq*OverlapReal(4.0) <= DaDb * DaDb);
    }

//! Define the general overlap function
/*! This is just a convenient spot to put this to make sure it is defined early
    \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \param err Incremented if there is an error condition. Left unchanged otherwise.
    \returns true when *a* and *b* overlap, and false when they are disjoint
*/
template <class ShapeA, class ShapeB>
DEVICE inline bool test_overlap(const vec3<Scalar>& r_ab, const ShapeA &a, const ShapeB& b, unsigned int& err)
    {
    // default implementation returns true, will make it obvious if something calls this
    return true;
    }

//! Sphere-Sphere overlap
/*! \param r_ab Vector defining the position of shape b relative to shape a (r_b - r_a)
    \param a first shape
    \param b second shape
    \param err in/out variable incremented when error conditions occur in the overlap test
    \returns true when *a* and *b* overlap, and false when they are disjoint

    \ingroup shape
*/
template <>
DEVICE inline bool test_overlap<ShapeSphere, ShapeSphere>(const vec3<Scalar>& r_ab,
    const ShapeSphere& a, const ShapeSphere& b, unsigned int& err)
    {
    vec3<OverlapReal> dr(r_ab);

    OverlapReal rsq = dot(dr,dr);

    OverlapReal RaRb = a.params.radius + b.params.radius;
    if (rsq < RaRb*RaRb)
        {
        return true;
        }
    else
        {
        return false;
        }
    }

namespace detail
{

//! APIs for depletant sampling
struct SamplingMethod {
    //! This API is used for fast sampling without the need for temporary storage
    enum enumNoStorage { no_storage = 0 };

    //! This API is used for accurate sampling, requiring temporary storage
    /* Any hit returned by excludedVolumeOverlap through this API *must* also
       also be a hit for the fast API
     */
    enum enumAccurate { accurate = 0 };
    };

};

//! Allocate memory for temporary storage in depletant simulations
/*! \param shape_a the first shape
    \param shape_b the second shape
    \param r_ab the separation vector between the two shapes (in the same image)
    \param r excluded volume radius
    \param dim the spatial dimension

    \returns the number of Shape::depletion_storage_type elements requested for
    temporary storage
 */
template<typename Method, class Shape>
DEVICE inline unsigned int allocateDepletionTemporaryStorage(
    const Shape& shape_a, const Shape& shape_b, const vec3<Scalar>& r_ab,
    OverlapReal r, unsigned int dim, const Method)
    {
    // default implementation doesn't require temporary storage
    return 0;
    }

//! Initialize temporary storage in depletant simulations
/*! \param shape_a the first shape
    \param shape_b the second shape
    \param r_ab the separation vector between the two shapes (in the same image)
    \param r excluded volume radius
    \param dim the spatial dimension
    \param storage a pointer to a pre-allocated memory region, the size of which has been
        pre-determined by a call to allocateDepletionTemporaryStorage
    \param V_sample the insertion volume
        V_sample has to to be precomputed for the overlapping shapes using
        getSamplingVolumeIntersection()

    \returns the number of Shape::depletion_storage_type elements initialized for temporary storage
 */
template<typename Method, class Shape>
DEVICE inline unsigned int initializeDepletionTemporaryStorage(
    const Shape& shape_a, const Shape& shape_b, const vec3<Scalar>& r_ab,
    OverlapReal r, unsigned int dim, typename Shape::depletion_storage_type *storage,
    const OverlapReal V_sample, const Method)
    {
    // default implementation doesn't require temporary storage
    return 0;
    }

//! Test for overlap of excluded volumes
/*! \param shape_a the first shape
    \param shape_b the second shape
    \param r_ab the separation vector between the two shapes (in the same image)
    \param r excluded volume radius
    \param dim the spatial dimension

    returns true if the covering of the intersection is non-empty
 */
template<typename Method, class Shape>
DEVICE inline bool excludedVolumeOverlap(
    const Shape& shape_a, const Shape& shape_b, const vec3<Scalar>& r_ab,
    OverlapReal r, unsigned int dim, const Method)
    {
    if (dim == 3)
        {
        OverlapReal Ra = OverlapReal(0.5)*shape_a.getCircumsphereDiameter()+r;
        OverlapReal Rb = OverlapReal(0.5)*shape_b.getCircumsphereDiameter()+r;

        return (dot(r_ab,r_ab) <= (Ra+Rb)*(Ra+Rb));
        }
    else
        {
        detail::AABB aabb_a = shape_a.getAABB(vec3<Scalar>(0.0,0.0,0.0));
        detail::AABB aabb_b = shape_b.getAABB(r_ab);

        // extend AABBs by the excluded volume radius
        vec3<Scalar> lower_a = aabb_a.getLower();
        vec3<Scalar> upper_a = aabb_a.getUpper();
        lower_a.x -= r; lower_a.y -= r; lower_a.z -= r;
        upper_a.x += r; upper_a.y += r; upper_a.z += r;

        vec3<Scalar> lower_b = aabb_b.getLower();
        vec3<Scalar> upper_b = aabb_b.getUpper();
        lower_b.x -= r; lower_b.y -= r; lower_b.z -= r;
        upper_b.x += r; upper_b.y += r; upper_b.z += r;

        return overlap(aabb_a,aabb_b);
        }
    }

//! Uniform rejection sampling in a volume covering the intersection of two shapes, defined by their Minkowski sums with a sphere of radius r
/*! \param rng random number generator
    \param shape_a the first shape
    \param shape_b the second shape
    \param r_ab the separation vector between the two shapes (in the same image)
    \param r excluded volume radius
    \param p the returned point (relative to the origin == shape_a)
    \param dim the spatial dimension
    \param storage_sz the number of temporary storage elements of type
        Shape::depletion_storage_type passed
    \param storage the array of temporary storage elements

    \returns true if the point was not rejected
 */
template<typename Method, class RNG, class Shape>
DEVICE inline bool sampleInExcludedVolumeIntersection(
    RNG& rng, const Shape& shape_a, const Shape& shape_b, const vec3<Scalar>& r_ab,
    OverlapReal r, vec3<OverlapReal>& p, unsigned int dim,
    unsigned int storage_sz, const typename Shape::depletion_storage_type *storage,
    const Method)
    {
    if (dim == 3)
        {
        OverlapReal Ra = OverlapReal(0.5)*shape_a.getCircumsphereDiameter()+r;
        OverlapReal Rb = OverlapReal(0.5)*shape_b.getCircumsphereDiameter()+r;

        if (dot(r_ab,r_ab) > (Ra+Rb)*(Ra+Rb))
            return false;

        vec3<OverlapReal> dr(r_ab);
        OverlapReal d = fast::sqrt(dot(dr,dr));

        // whether the intersection is the entire (smaller) sphere
        bool sphere = (d + Ra - Rb < OverlapReal(0.0)) || (d + Rb - Ra < OverlapReal(0.0));

        if (!sphere)
            {
            // heights spherical caps that constitute the intersection volume
            OverlapReal ha = (Rb*Rb - (d-Ra)*(d-Ra))/(OverlapReal(2.0)*d);
            OverlapReal hb = (Ra*Ra - (d-Rb)*(d-Rb))/(OverlapReal(2.0)*d);

            // volumes of spherical caps
            OverlapReal Vcap_a = OverlapReal(M_PI/3.0)*ha*ha*(OverlapReal(3.0)*Ra-ha);
            OverlapReal Vcap_b = OverlapReal(M_PI/3.0)*hb*hb*(OverlapReal(3.0)*Rb-hb);

            // choose one of the two caps randomly, with a weight proportional to their volume
            hoomd::UniformDistribution<OverlapReal> u;
            OverlapReal s = u(rng);
            bool cap_a = s < Vcap_a/(Vcap_a+Vcap_b);

            // generate a depletant position in the spherical cap
            if (cap_a)
                p = generatePositionInSphericalCap(rng, vec3<Scalar>(0.0,0.0,0.0), Ra, ha, dr);
            else
                p = generatePositionInSphericalCap(rng, dr, Rb, hb, -dr);
            }
        else
            {
            // generate a random position in the smaller sphere
            if (Ra < Rb)
                {
                p = generatePositionInSphere(rng, vec3<Scalar>(0.0,0.0,0.0), Ra);
                }
            else
                {
                p = vec3<OverlapReal>(generatePositionInSphere(rng, dr, Rb));
                }
            }

        // sphere (cap) sampling is rejection free
        return true;
        }
    else
        {
        detail::AABB aabb_a = shape_a.getAABB(vec3<Scalar>(0.0,0.0,0.0));
        detail::AABB aabb_b = shape_b.getAABB(r_ab);

        if (!overlap(aabb_a, aabb_b))
            return false;

        // extend AABBs by the excluded volume radius
        vec3<Scalar> lower_a = aabb_a.getLower();
        vec3<Scalar> upper_a = aabb_a.getUpper();
        lower_a.x -= r; lower_a.y -= r; lower_a.z -= r;
        upper_a.x += r; upper_a.y += r; upper_a.z += r;

        vec3<Scalar> lower_b = aabb_b.getLower();
        vec3<Scalar> upper_b = aabb_b.getUpper();
        lower_b.x -= r; lower_b.y -= r; lower_b.z -= r;
        upper_b.x += r; upper_b.y += r; upper_b.z += r;

        // we already know the AABBs are overlapping, compute their intersection
        vec3<Scalar> intersect_lower, intersect_upper;
        intersect_lower.x = detail::max(lower_a.x, lower_b.x);
        intersect_lower.y = detail::max(lower_a.y, lower_b.y);
        intersect_lower.z = detail::max(lower_a.z, lower_b.z);
        intersect_upper.x = detail::min(upper_a.x, upper_b.x);
        intersect_upper.y = detail::min(upper_a.y, upper_b.y);
        intersect_upper.z = detail::min(upper_a.z, upper_b.z);

        detail::AABB aabb_intersect(intersect_lower, intersect_upper);
        p = vec3<OverlapReal>(generatePositionInAABB(rng, aabb_intersect, dim));

        // AABB sampling always succeeds
        return true;
        }
    }

//! Get the sampling volume for an intersection of shapes
/*! \param shape_a the first shape
    \param shape_b the second shape
    \param r_ab the separation vector between the two shapes (in the same image)
    \param r excluded volume radius
    \param p the returned point
    \param dim the spatial dimension

    If the shapes are not overlapping, return zero.

    returns the volume of the intersection
 */
template<typename Method, class Shape>
DEVICE inline OverlapReal getSamplingVolumeIntersection(
    const Shape& shape_a, const Shape& shape_b, const vec3<Scalar>& r_ab,
    OverlapReal r, unsigned int dim, const Method)
    {
    if (dim == 3)
        {
        OverlapReal Ra = OverlapReal(0.5)*shape_a.getCircumsphereDiameter()+r;
        OverlapReal Rb = OverlapReal(0.5)*shape_b.getCircumsphereDiameter()+r;

        if (dot(r_ab,r_ab) > (Ra+Rb)*(Ra+Rb))
            return OverlapReal(0.0);

        vec3<OverlapReal> dr(r_ab);
        OverlapReal d = fast::sqrt(dot(dr,dr));

        if ((d + Ra - Rb < OverlapReal(0.0)) || (d + Rb - Ra < OverlapReal(0.0)))
            {
            // the intersection is the entire (smaller) sphere
            return (Ra < Rb) ? OverlapReal(M_PI*4.0/3.0)*Ra*Ra*Ra : OverlapReal(M_PI*4.0/3.0)*Rb*Rb*Rb;
            }
        else
            {
            // heights spherical caps that constitute the intersection volume
            OverlapReal ha = (Rb*Rb - (d-Ra)*(d-Ra))/(OverlapReal(2.0)*d);
            OverlapReal hb = (Ra*Ra - (d-Rb)*(d-Rb))/(OverlapReal(2.0)*d);

            // volumes of spherical caps
            OverlapReal Vcap_a = OverlapReal(M_PI/3.0)*ha*ha*(OverlapReal(3.0)*Ra-ha);
            OverlapReal Vcap_b = OverlapReal(M_PI/3.0)*hb*hb*(OverlapReal(3.0)*Rb-hb);

            // volume of intersection
            return Vcap_a + Vcap_b;
            }
        }
    else
        {
        detail::AABB aabb_a = shape_a.getAABB(vec3<Scalar>(0.0,0.0,0.0));
        detail::AABB aabb_b = shape_b.getAABB(r_ab);

        if (!overlap(aabb_a, aabb_b))
            return OverlapReal(0.0);

        // extend AABBs by the excluded volume radius
        vec3<Scalar> lower_a = aabb_a.getLower();
        vec3<Scalar> upper_a = aabb_a.getUpper();
        lower_a.x -= r; lower_a.y -= r; lower_a.z -= r;
        upper_a.x += r; upper_a.y += r; upper_a.z += r;

        vec3<Scalar> lower_b = aabb_b.getLower();
        vec3<Scalar> upper_b = aabb_b.getUpper();
        lower_b.x -= r; lower_b.y -= r; lower_b.z -= r;
        upper_b.x += r; upper_b.y += r; upper_b.z += r;

        // we already know the AABBs are overlapping, compute their intersection
        vec3<Scalar> intersect_lower, intersect_upper;
        intersect_lower.x = detail::max(lower_a.x, lower_b.x);
        intersect_lower.y = detail::max(lower_a.y, lower_b.y);
        intersect_lower.z = detail::max(lower_a.z, lower_b.z);
        intersect_upper.x = detail::min(upper_a.x, upper_b.x);
        intersect_upper.y = detail::min(upper_a.y, upper_b.y);
        intersect_upper.z = detail::min(upper_a.z, upper_b.z);

        // intersection AABB volume
        OverlapReal V =  (intersect_upper.x-intersect_lower.x)*(intersect_upper.y-intersect_lower.y);
        if(dim == 3)
            V *= intersect_upper.z-intersect_lower.z;
        return V;
        }
    }

//! Test if a point is in the intersection of two excluded volumes
/*! \param shape_a the first shape
    \param shape_b the second shape
    \param r_ab the separation vector between the two shapes (in the same image)
    \param r excluded volume radius
    \param p the point to test (relative to the origin == shape_a)
    \param dim the spatial dimension

    It is assumed that the circumspheres of the shapes are overlapping, otherwise the result is invalid

    The point p is in the world frame, with shape a at the origin

    returns true if the point was not rejected
 */
template<typename Method, class Shape>
DEVICE inline bool isPointInExcludedVolumeIntersection(
    const Shape& shape_a, const Shape& shape_b, const vec3<Scalar>& r_ab,
    OverlapReal r, const vec3<OverlapReal>& p, unsigned int dim, const Method)
    {
    if (dim == 3)
        {
        OverlapReal Ra = OverlapReal(0.5)*shape_a.getCircumsphereDiameter()+r;
        OverlapReal Rb = OverlapReal(0.5)*shape_b.getCircumsphereDiameter()+r;
        vec3<OverlapReal> dr(r_ab);

        bool is_pt_in_sphere_a = dot(p,p) <= Ra*Ra;
        bool is_pt_in_sphere_b = dot(p-dr,p-dr) <= Rb*Rb;

        // point has to be in the intersection of both spheres
        return is_pt_in_sphere_a && is_pt_in_sphere_b;
        }
    else
        {
        detail::AABB aabb_a = shape_a.getAABB(vec3<Scalar>(0.0,0.0,0.0));
        detail::AABB aabb_b = shape_b.getAABB(r_ab);

        // extend AABBs by the excluded volume radius
        vec3<Scalar> lower_a = aabb_a.getLower();
        vec3<Scalar> upper_a = aabb_a.getUpper();
        lower_a.x -= r; lower_a.y -= r; lower_a.z -= r;
        upper_a.x += r; upper_a.y += r; upper_a.z += r;

        vec3<Scalar> lower_b = aabb_b.getLower();
        vec3<Scalar> upper_b = aabb_b.getUpper();
        lower_b.x -= r; lower_b.y -= r; lower_b.z -= r;
        upper_b.x += r; upper_b.y += r; upper_b.z += r;

        // we already know the AABBs are overlapping, compute their intersection
        vec3<Scalar> intersect_lower, intersect_upper;
        intersect_lower.x = detail::max(lower_a.x, lower_b.x);
        intersect_lower.y = detail::max(lower_a.y, lower_b.y);
        intersect_lower.z = detail::max(lower_a.z, lower_b.z);
        intersect_upper.x = detail::min(upper_a.x, upper_b.x);
        intersect_upper.y = detail::min(upper_a.y, upper_b.y);
        intersect_upper.z = detail::min(upper_a.z, upper_b.z);

        detail::AABB aabb_intersect(intersect_lower, intersect_upper);

        return intersect_lower.x <= p.x && p.x <= intersect_upper.x &&
               intersect_lower.y <= p.y && p.y <= intersect_upper.y &&
               ((dim == 2) || (intersect_lower.z <= p.z && p.z <= intersect_upper.z));
        }
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
#endif //__SHAPE_SPHERE_H__
