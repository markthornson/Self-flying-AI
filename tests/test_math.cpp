#include "approx.h"

#include "engine/core/math/math.h"

using namespace eng;

TEST_CASE("vector basics") {
    Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    CHECK(dot(x, y) == 0.0f);
    CHECK(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == 32.0f);
    // Right-handed: X cross Y is Z.
    CHECK(cross(x, y) == z);
    CHECK(cross(y, z) == x);
    CHECK(length(Vec3{3, 4, 0}) == doctest::Approx(5.0f));
    check_near(normalize(Vec3{0, 0, 10}), z);
    CHECK(normalize(Vec3{}) == Vec3{}); // no NaNs from a zero vector
    check_near(lerp(Vec3{0, 0, 0}, Vec3{10, 20, 30}, 0.5f), Vec3{5, 10, 15});
}

TEST_CASE("quaternion rotation") {
    Quat q = quat_from_axis_angle({0, 1, 0}, radians(90.0f));
    // A quarter turn about +Y takes +X to -Z (counter-clockwise seen from above).
    check_near(rotate(q, Vec3{1, 0, 0}), Vec3{0, 0, -1});
    check_near(rotate(Quat{}, Vec3{1, 2, 3}), Vec3{1, 2, 3});

    SUBCASE("composition applies the right-hand rotation first") {
        Quat about_y = quat_from_axis_angle({0, 1, 0}, radians(90.0f));
        Quat about_x = quat_from_axis_angle({1, 0, 0}, radians(90.0f));
        Vec3 v{0, 0, 1};
        check_near(rotate(about_y * about_x, v), rotate(about_y, rotate(about_x, v)));
    }

    SUBCASE("slerp goes half way") {
        Quat a{};
        Quat b = quat_from_axis_angle({0, 0, 1}, radians(90.0f));
        check_near(rotate(slerp(a, b, 0.5f), Vec3{1, 0, 0}),
                   Vec3{std::sqrt(0.5f), std::sqrt(0.5f), 0});
        check_near(rotate(slerp(a, b, 0.0f), Vec3{1, 0, 0}), Vec3{1, 0, 0});
        check_near(rotate(slerp(a, b, 1.0f), Vec3{1, 0, 0}), Vec3{0, 1, 0});
    }
}

TEST_CASE("euler angles round-trip through a quaternion") {
    // Pitch, yaw and roll each on their own, then all three together.
    const Vec3 cases[] = {{0.3f, 0, 0}, {0, 1.2f, 0}, {0, 0, -0.7f}, {0.4f, -2.5f, 0.9f}};
    for (Vec3 angles : cases) {
        Quat q = quat_from_euler(angles);
        check_near(euler_from_quat(q), angles, 1e-4);
    }
    // Yaw alone is a plain turn about +Y.
    check_near(rotate(quat_from_euler({0, radians(90.0f), 0}), Vec3{1, 0, 0}), Vec3{0, 0, -1});
    // Yaw is applied last: pitching first doesn't change which way it faces.
    Vec3 forward = rotate(quat_from_euler({radians(30.0f), radians(90.0f), 0}), Vec3{0, 0, 1});
    CHECK(forward.z == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(forward.x > 0.0f);
}

TEST_CASE("matrix and quaternion agree") {
    Quat q = normalize(quat_from_axis_angle({1, 2, 3}, 1.234f));
    Vec3 v{0.5f, -2.0f, 4.0f};
    check_near(transform_direction(mat4_rotation(q), v), rotate(q, v));
}

TEST_CASE("matrix composition") {
    Mat4 t = mat4_translation({10, 0, 0});
    Mat4 s = mat4_scale({2, 2, 2});
    // (t * s) scales first, then translates.
    check_near(transform_point(t * s, Vec3{1, 0, 0}), Vec3{12, 0, 0});
    check_near(transform_point(s * t, Vec3{1, 0, 0}), Vec3{22, 0, 0});
    // Directions ignore translation.
    check_near(transform_direction(t, Vec3{1, 0, 0}), Vec3{1, 0, 0});

    Mat4 m = mat4_translation({1, 2, 3}) * mat4_rotation(quat_from_axis_angle({0, 1, 0}, 0.7f));
    Mat4 identity_check = m * mat4_identity();
    for (int c = 0; c < 4; ++c) check_near(identity_check.cols[c], m.cols[c]);
    CHECK(transpose(transpose(m)).at(0, 3) == m.at(0, 3));
    CHECK(m.at(0, 3) == 1.0f); // translation sits in the last column
}

TEST_CASE("transform builds translation * rotation * scale") {
    Transform tr;
    tr.position = {0, 0, 5};
    tr.rotation = quat_from_axis_angle({0, 1, 0}, radians(90.0f));
    tr.scale = {2, 2, 2};
    // Scale (1,0,0) to (2,0,0), rotate to (0,0,-2), then move to (0,0,3).
    check_near(transform_point(tr.to_matrix(), Vec3{1, 0, 0}), Vec3{0, 0, 3});

    Transform a, b;
    b.position = {10, 0, 0};
    check_near(interpolate(a, b, 0.25f).position, Vec3{2.5f, 0, 0});
}

TEST_CASE("look_at puts the target straight ahead") {
    Mat4 view = mat4_look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    // The camera sits at the origin of view space, looking down -Z.
    check_near(transform_point(view, Vec3{0, 0, 5}), Vec3{0, 0, 0});
    check_near(transform_point(view, Vec3{0, 0, 0}), Vec3{0, 0, -5});
    check_near(transform_point(view, Vec3{1, 0, 5}), Vec3{1, 0, 0}); // right stays right

    Mat4 view2 = mat4_look_at({3, 4, 5}, {1, 1, 1}, {0, 1, 0});
    Vec3 ahead = transform_point(view2, Vec3{1, 1, 1});
    CHECK(ahead.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(ahead.y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(ahead.z < 0.0f);
}

TEST_CASE("perspective maps near to depth 0 and far to depth 1") {
    float n = 0.1f, f = 100.0f;
    Mat4 p = mat4_perspective(radians(90.0f), 1.0f, n, f);
    auto ndc = [&](Vec3 view_pos) {
        Vec4 clip = p * to_vec4(view_pos, 1.0f);
        return Vec3{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
    };
    CHECK(ndc({0, 0, -n}).z == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(ndc({0, 0, -f}).z == doctest::Approx(1.0f).epsilon(1e-4));
    // With a 90 degree field of view, a point at 45 degrees up lands on the top edge.
    CHECK(ndc({0, 10, -10}).y == doctest::Approx(1.0f));
    CHECK(ndc({0, 10, -10}).x == doctest::Approx(0.0f));
}
