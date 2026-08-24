#pragma once

/**
    The GLSL, as string literals.

    `#version 410 core` because that is macOS's ceiling and the rest of the fleet
    already sits there. Nothing here needs 4.3.

    ## Per-instance data goes through vertex attributes, not uniform arrays

    flipbook and orrery upload their per-copy data as uniform arrays and index
    them by `gl_InstanceID`, which is tidy and works well for a few dozen copies.
    It cannot work here. A cell needs a full `mat4` -- sixteen floats -- and a
    6-cube is 384 cells, so the uniform array would want 6144 floats before the
    UV transforms are counted. GL 4.1 guarantees about a thousand vec4s of
    vertex uniform storage. It would compile on the development machine, fit at
    a 4x4 grid, and fail on somebody's wall.

    So the instance data is a VBO with `glVertexAttribDivisor`, six attribute
    slots of the sixteen guaranteed: four for the matrix, one for the UV
    transform, one for the per-cell scalars.

    ## The quad has no vertex buffer

    `gl_VertexID` generates the corners of a unit quad for a four-vertex triangle
    strip. There is no position attribute and no element buffer, which means the
    only thing bound at draw time is the instance data.
*/
namespace gridiron
{
extern const char* const kWallVertexShader;
extern const char* const kWallFragmentShader;

/// The full-screen pass: composites gridiron's own framebuffer into the host's,
/// and applies the global edge treatment on the way through if there is one.
extern const char* const kCompositeVertexShader;
extern const char* const kCompositeFragmentShader;

} // namespace gridiron
