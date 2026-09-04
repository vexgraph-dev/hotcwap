#!/bin/sh
# build_shaders.sh — recompile GLSL sources into the SPIR-V blobs the engine
# loads at runtime (vulkan.c loadSpvAny -> src/vulkan/spv/*.spv).
#
# Why both exist: Vulkan consumes SPIR-V bytecode, never GLSL text; the .spv
# files are the deployable artifacts, these .vert/.frag files are their source.
# Edit the GLSL here, then run this script before rebuilding the engine.
#
# Core shaders only (hello_triangle, solid_quad). UI shaders
# (texture_quad, text_sdf, sdf_*) live in darling with their own script.
#
# Requires: glslangValidator (brew install glslang).
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"

glslangValidator -V "$DIR/hello_triangle.vert" -o "$DIR/../spv/hello_triangle_vert.spv"
glslangValidator -V "$DIR/hello_triangle.frag" -o "$DIR/../spv/hello_triangle_frag.spv"
glslangValidator -V "$DIR/solid_quad.vert" -o "$DIR/../spv/solid_quad_vert.spv"
glslangValidator -V "$DIR/solid_quad.frag" -o "$DIR/../spv/solid_quad_frag.spv"

echo "shaders: spv refreshed"
