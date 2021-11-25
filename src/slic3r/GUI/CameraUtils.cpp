#include "CameraUtils.hpp"
#include <igl/project.h> // projecting points

#include "slic3r/GUI/3DScene.hpp" // GLVolume
#include "libslic3r/Geometry/ConvexHull.hpp"

using namespace Slic3r;
using namespace GUI;

Points CameraUtils::project(const Camera &            camera,
                            const std::vector<Vec3d> &points)
{
    Vec4i viewport(camera.get_viewport().data());

    // Convert our std::vector to Eigen dynamic matrix.
    Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::DontAlign>
        pts(points.size(), 3);
    for (size_t i = 0; i < points.size(); ++i)
        pts.block<1, 3>(i, 0) = points[i];

    // Get the projections.
    Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::DontAlign> projections;
    igl::project(pts, camera.get_view_matrix().matrix(),
                    camera.get_projection_matrix().matrix(), viewport, projections);

    Points result;
    result.reserve(points.size());
    int window_height = viewport[3];

    // Iterate over all points and determine whether they're in the rectangle.
    for (int i = 0; i < projections.rows(); ++i) {
        double x = projections(i, 0);
        double y = projections(i, 1);
        // opposit direction o Y
        result.emplace_back(x, window_height - y);
    }
    return result;
}

Slic3r::Polygon CameraUtils::create_hull2d(const Camera &  camera,
                                   const GLVolume &volume)
{
    std::vector<Vec3d>  vertices;
    const TriangleMesh *hull = volume.convex_hull();
    if (hull != nullptr) {
        const indexed_triangle_set &its = hull->its;        
        vertices.reserve(its.vertices.size());
        // cast vector
        for (const Vec3f &vertex : its.vertices)
            vertices.emplace_back(vertex.cast<double>());
    } else {
        // Negative volume doesn't have convex hull so use bounding box
        auto bb = volume.bounding_box();
        Vec3d &min = bb.min;
        Vec3d &max = bb.max;
        vertices   = {min,
                    Vec3d(min.x(), min.y(), max.z()),
                    Vec3d(min.x(), max.y(), min.z()),
                    Vec3d(min.x(), max.y(), max.z()),
                    Vec3d(max.x(), min.y(), min.z()),
                    Vec3d(max.x(), min.y(), max.z()),
                    Vec3d(max.x(), max.y(), min.z()),
                    max};
    }

    const Transform3d &trafoMat =
        volume.get_instance_transformation().get_matrix() *
        volume.get_volume_transformation().get_matrix();
    for (Vec3d &vertex : vertices)
        vertex = trafoMat * vertex.cast<double>();

    Points vertices_2d = project(camera, vertices);
    return Geometry::convex_hull(vertices_2d);
}
