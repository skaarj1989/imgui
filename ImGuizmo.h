// The MIT License(MIT)
//
// Copyright(c) 2016 Cedric Guillemet
// Copyright(c) 2020 Dawid Kurek
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "imgui.h"

enum ImGuizmoCol_ {
  ImGuizmoCol_Text,
  ImGuizmoCol_TextShadow,

  ImGuizmoCol_Inactive,
  ImGuizmoCol_Hovered,

  ImGuizmoCol_SpecialMove,

  ImGuizmoCol_AxisX,
  ImGuizmoCol_AxisY,
  ImGuizmoCol_AxisZ,

  ImGuizmoCol_PlaneYZ,
  ImGuizmoCol_PlaneZX,
  ImGuizmoCol_PlaneXY,

  ImGuizmoCol_BoundAnchor,

  ImGuizmoCol_COUNT
};
using ImGuizmoCol = int;

struct ImGuizmoStyle {
  float GizmoScale{ 0.1f };
  float RotationRingThickness{ 3.5f };

  float Alpha{ 1.0f };
  ImVec4 Colors[ImGuizmoCol_COUNT];

  IMGUI_API ImGuizmoStyle();
};

enum ImGuizmoMode_ {
  ImGuizmoMode_World, // Transform tools aligned to world grid
  ImGuizmoMode_Local,  // Transform tools aligned to rotation of model matrix

  ImGuizmoMode_COUNT
};
using ImGuizmoMode = int;

enum ImGuizmoOperation_ {
  ImGuizmoOperation_None = 0,

  ImGuizmoOperation_Translate,
  ImGuizmoOperation_Rotate,
  ImGuizmoOperation_Scale,
  ImGuizmoOperation_BoundsScale,
};
using ImGuizmoOperation = int;

enum ImGuizmoAxisFlags_ {
  ImGuizmoAxisFlags_None = 0,

  ImGuizmoAxisFlags_X = 1 << 0,
  ImGuizmoAxisFlags_Y = 1 << 1,
  ImGuizmoAxisFlags_Z = 1 << 2,

  ImGuizmoAxisFlags_YZ = ImGuizmoAxisFlags_Y | ImGuizmoAxisFlags_Z,
  ImGuizmoAxisFlags_ZX = ImGuizmoAxisFlags_Z | ImGuizmoAxisFlags_X,
  ImGuizmoAxisFlags_XY = ImGuizmoAxisFlags_X | ImGuizmoAxisFlags_Y,

  ImGuizmoAxisFlags_ALL =
    ImGuizmoAxisFlags_X | ImGuizmoAxisFlags_Y | ImGuizmoAxisFlags_Z
};
using ImGuizmoAxisFlags = unsigned int;

enum ImGuizmoConfigFlags_ {
  ImGuizmoConfigFlags_None = 0,

  ImGuizmoConfigFlags_CloakOnManipulate = 1 << 0, // Render only active manipulation
  ImGuizmoConfigFlags_HideLocked        = 1 << 1, // Hides locked axes instead of drawing as inactive
  ImGuizmoConfigFlags_HasReversing      = 1 << 2  // Cancel active manipulation on RMB
};
using ImGuizmoConfigFlags = unsigned int;

namespace ImGuizmo {

IMGUI_API void PrintContext();

IMGUI_API ImGuizmoStyle &GetStyle();
IMGUI_API void StyleColorsClassic(ImGuizmoStyle *dst = nullptr);
IMGUI_API void StyleColorsBlender(ImGuizmoStyle *dst = nullptr);
IMGUI_API void StyleColorsUnreal(ImGuizmoStyle *dst = nullptr);

IMGUI_API void ShowStyleEditor(ImGuizmoStyle *ref = nullptr);
IMGUI_API bool ShowStyleSelector(const char *label);

IMGUI_API void SetConfigFlags(ImGuizmoConfigFlags flags);

IMGUI_API void SetViewport(const ImVec2 &position, const ImVec2 &size);
IMGUI_API void SetViewport(float x, float y, float width, float height);
/** @param draw_list */
IMGUI_API void SetDrawlist(ImDrawList *draw_list = nullptr);

/**
 * @param [in] view_matrix Camera view matrix (column-major)
 * @param [in] projection_matrix Camera projection matrix (column-major)
 */
IMGUI_API void SetCamera(const float *view_matrix,
                         const float *projection_matrix, bool is_ortho);

/**
 * @note Convenience method
 * @param [in] snap shared between all operations
 * @return same as End()
 */
IMGUI_API bool Manipulate(ImGuizmoMode mode, ImGuizmoOperation operation,
                          float *model_matrix, const float *snap = nullptr);

/** 
 * @param [in] model_matrix Model matrix (column-major)
 * @return true if gizmo is visible
 */
IMGUI_API bool Begin(ImGuizmoMode mode, float *model_matrix,
                     ImGuizmoAxisFlags locked_axes = ImGuizmoAxisFlags_None);
/**
 * Saves result to locked model matrix if manipulation has been made between
 * Begin/End
 * @return true if matrix has been updated
 */
IMGUI_API bool End();

/** @param snap */
IMGUI_API void Translate(const float *snap = nullptr);
/** @param snap */
IMGUI_API void Rotate(const float *snap = nullptr);
/** @param snap */
IMGUI_API void Scale(const float *snap = nullptr);

/** @param snap */
IMGUI_API void BoundsScale(const float *bounds, const float *snap = nullptr);

/**
 * @note Please note that this cubeview is patented by Autodesk:
 * https://patents.google.com/patent/US7782319B2/en It seems to be a defensive
 * patent in the US. I don't think it will bring troubles using it as other
 * software are using the same mechanics. But just in case, you are now warned!
 *
 * @param [in] view_matrix Camera view, column-major matrix
 */
IMGUI_API void ViewManipulate(float *view_matrix, const float length,
                              const ImVec2 &position, const ImVec2 &size,
                              ImU32 background_color = 0x10101010);

IMGUI_API void ViewManipulate(float *view_matrix, const float length,
                              ImU32 background_color = 0x10101010);

//
//
//

/**
 * @param [in] matrix Input matrix (column-major)
 * @param [out] t vec3 Translation
 * @param [out] r vec3 Rotation in degrees
 * @param [out] s vec3 Scale
 */
IMGUI_API void DecomposeMatrix(const float *matrix, float *t, float *r,
                               float *s);
/**
 * @param [in] t vec3 Translation
 * @param [in] r vec3 Rotation in degrees
 * @param [in] s vec3 Scale
 * @param [out] matrix Result matrix (column-major)
 */
IMGUI_API void RecomposeMatrix(const float *t, const float *r, const float *s,
                               float *matrix);

}; // namespace ImGuizmo