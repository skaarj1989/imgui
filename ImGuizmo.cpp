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

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#  define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "ImGuizmo.h"
#include "imgui_internal.h"

#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/common.hpp"
#include "glm/gtx/compatibility.hpp"
#include "glm/gtx/transform.hpp"

//-----------------------------------------------------------------------------
// [SECTION] CONSTANTS
//-----------------------------------------------------------------------------

constexpr auto kPi = glm::pi<float>();
constexpr auto kEpsilon = glm::epsilon<float>();

static const glm::vec3 kReferenceUp{ 0.0f, 1.0f, 0.0f };
static const glm::vec3 kUnitDirections[3]{
  { 1.0f, 0.0f, 0.0f }, // Right
  { 0.0f, 1.0f, 0.0f }, // Up
  { 0.0f, 0.0f, 1.0f }  // Forward
};

// Size of the quads responsible for movement on a plane
constexpr float kQuadSize{ 0.20f };
constexpr float kQuadMin{ 0.30f };
constexpr float kQuadMax{ kQuadMin + kQuadSize };
static const float kUnitQuad[]{ kQuadMin, kQuadMin, kQuadMin, kQuadMax,
                                kQuadMax, kQuadMax, kQuadMax, kQuadMin };

constexpr float kCircleRadius{ 6.0f };  // Translation and scale dots
constexpr float kLineThickness{ 3.0f }; // Translation and scale axes
constexpr int kCircleSegmentCount{ 128 };

constexpr float kOutterAnchorSize{ 6.0f };
constexpr float kMidAnchorSize{ 4.0f };

//-----------------------------------------------------------------------------
// [SECTION] INTERNAL TYPES
//-----------------------------------------------------------------------------

enum ImGuizmoAxis_ {
  ImGuizmoAxis_X,
  ImGuizmoAxis_Y,
  ImGuizmoAxis_Z,

  ImGuizmoAxis_COUNT
};
using ImGuizmoAxis = int;

// @note Plane index is also an index to axis that is normal to that plane
enum ImGuizmoPlane_ {
  ImGuizmoPlane_YZ,
  ImGuizmoPlane_ZX,
  ImGuizmoPlane_XY,

  ImGuizmoPlane_COUNT
};
using ImGuizmoPlane = int;

struct ImGuizmoRay {
  glm::vec3 Origin{ 0.0f };
  glm::vec3 End{ 0.0f };
  glm::vec3 Direction{ 0.0f };
};
struct ImGuizmoCamera {
  bool IsOrtho{ false };

  glm::mat4 ViewMatrix{ 1.0f };
  glm::mat4 ProjectionMatrix{ 1.0f };
  glm::mat4 ViewProjectionMatrix{ 1.0f };

  glm::vec3 Right{ kUnitDirections[0] };
  glm::vec3 Up{ kUnitDirections[1] };
  glm::vec3 Forward{ kUnitDirections[2] };
  glm::vec3 Eye{ 0.0f };
};

struct ImGuizmoWidget {
  ImGuiID ID{ 0 };

  // Used as reference model matrix, doesn't change while manipulating
  glm::mat4 SourceModelMatrix{ 1.0f };
  glm::mat4 ModelMatrix{ 1.0f };
  glm::mat4 InversedModelMatrix{ 1.0f };

  glm::mat4 ModelViewProjMatrix{ 1.0f };

  ImGuizmoMode Mode{ ImGuizmoMode_Local };
  // Translate/ Rotate/ Scale
  ImGuizmoOperation ActiveOperation{ ImGuizmoOperation_None };
  ImGuizmoAxisFlags ActiveManipulationFlags{ ImGuizmoAxisFlags_None };
  ImGuizmoAxisFlags LockedAxesFlags{ ImGuizmoAxisFlags_None };

  bool Dirty{ false }; // It's set to true on manipulate

  // Screen space values
  glm::vec2 Origin{ 0.0f };
  float RingRadius{ 0.0f };
  float ScreenFactor{ 0.0f };

  // Shared across transformations
  glm::vec4 TranslationPlane{ 0.0f };       // T+R+S
  glm::vec3 TranslationPlaneOrigin{ 0.0f }; // T+S
  glm::vec3 ModelRelativeOrigin{ 0.0f };    // T+S
  glm::vec3 DragTranslationOrigin{ 0.0f };  // T+S

  // Translation
  glm::vec3 LastTranslationDelta{ 0.0f };

  // Rotation
  glm::vec3 ModelScaleOrigin{ 1.0f };
  glm::vec3 RotationVectorSource{ 0.0f };
  float RotationAngle{ 0.0f };       // In radians
  float RotationAngleOrigin{ 0.0f }; // In radians

  // Scale
  glm::vec3 Scale{ 1.0f };
  glm::vec3 LastScale{ 1.0f };
  glm::vec3 ScaleValueOrigin{ 1.0f };

  // ---

  explicit ImGuizmoWidget(ImGuiID id) : ID{ id } {}
  void Load(const float *model);
  float CalculateAngleOnPlane() const;
};

struct ImGuizmoBounds {
  glm::vec3 OutterPoints[3][4]{};
  glm::vec3 MidPoints[3][4]{};

  glm::vec3 Anchor{ 0.0f };
  glm::vec3 LocalPivot{ 0.0f };
  glm::vec3 Pivot{ 0.0f };

  ImGuizmoPlane ActivePlane{ -1 };
  int ActiveBoundIdx{ -1 };
};

//-----------------------------------------------------------------------------
// [SECTION] CONTEXT
//-----------------------------------------------------------------------------

struct ImGuizmoContext {
  ImDrawList *DrawList{ nullptr };

  bool Enabled{ true };

  ImGuizmoStyle Style;
  ImGuizmoConfigFlags ConfigFlags{ ImGuizmoConfigFlags_None };

  ImRect Viewport;
  ImGuizmoCamera Camera;
  ImGuizmoRay Ray;
  glm::vec2 DragOrigin{ 0.0f };

  ImVector<ImGuizmoWidget *> Gizmos;
  ImGuiStorage GizmosById;

  ImGuizmoWidget *CurrentGizmo{ nullptr }; // Gizmo in Begin/End scope
  ImGuizmoWidget *ActiveGizmo{ nullptr };  // Currently manipulated gizmo

  ImGuizmoBounds Bounds;

  float PlanesVisibility[3]{ 0.0f }; // 0 = invisible, 1 = most visible
  ImGuizmoPlane MostVisiblePlanes[3]{ 0, 1, 2 };

  float *LockedModelMatrix{ nullptr };
  glm::mat4 BackupModelMatrix{ 1.0f }; // For reverting operation

  // ---

  ImGuizmoContext() = default;
  ~ImGuizmoContext();

  float GetAspectRatio() const;
};
static ImGuizmoContext GImGuizmo;

ImGuizmoWidget *FindGizmoById(ImGuiID id) {
  return static_cast<ImGuizmoWidget *>(GImGuizmo.GizmosById.GetVoidPtr(id));
}
ImGuizmoWidget *CreateNewGizmo(ImGuiID id) {
  ImGuizmoContext &g{ GImGuizmo };
  auto *gizmo = new ImGuizmoWidget(id);
  g.GizmosById.SetVoidPtr(id, gizmo);
  g.Gizmos.push_back(gizmo);
  return gizmo;
}
ImGuizmoWidget *GetCurrentGizmo() { return GImGuizmo.CurrentGizmo; }

ImGuizmoStyle::ImGuizmoStyle() { ImGuizmo::StyleColorsClassic(this); }

namespace ImGuizmo {

//-----------------------------------------------------------------------------
// [SECTION] STYLING
//-----------------------------------------------------------------------------

ImGuizmoStyle &GetStyle() { return GImGuizmo.Style; }
void StyleColorsClassic(ImGuizmoStyle *dst) {
  ImGuizmoStyle *style{ dst ? dst : &ImGuizmo::GetStyle() };
  ImVec4 *colors{ style->Colors };

  colors[ImGuizmoCol_Text] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };
  colors[ImGuizmoCol_TextShadow] = ImVec4{ 0.0f, 0.0f, 0.0f, 1.0f };

  colors[ImGuizmoCol_Inactive] = ImVec4{ 0.6f, 0.6f, 0.6f, 0.6f };
  colors[ImGuizmoCol_Hovered] = ImVec4{ 1.0f, 0.5f, 0.06f, 0.54f };

  colors[ImGuizmoCol_SpecialMove] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };

  colors[ImGuizmoCol_AxisX] = ImVec4{ 0.66f, 0.0f, 0.0f, 1.0f };
  colors[ImGuizmoCol_AxisY] = ImVec4{ 0.0f, 0.66f, 0.0f, 1.0f };
  colors[ImGuizmoCol_AxisZ] = ImVec4{ 0.0f, 0.0f, 0.66f, 1.0f };

  colors[ImGuizmoCol_PlaneYZ] = ImVec4{ 0.66f, 0.0f, 0.0f, 0.38f };
  colors[ImGuizmoCol_PlaneZX] = ImVec4{ 0.0f, 0.66f, 0.0f, 0.38f };
  colors[ImGuizmoCol_PlaneXY] = ImVec4{ 0.0f, 0.0f, 0.66f, 0.38f };

  colors[ImGuizmoCol_BoundAnchor] = ImVec4{ 0.66f, 0.66f, 0.66f, 1.0f };
}
void StyleColorsBlender(ImGuizmoStyle *dst) {
  ImGuizmoStyle *style{ dst ? dst : &ImGuizmo::GetStyle() };
  ImVec4 *colors{ style->Colors };

  colors[ImGuizmoCol_Text] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };
  colors[ImGuizmoCol_TextShadow] = ImVec4{ 0.0f, 0.0f, 0.0f, 1.0f };

  colors[ImGuizmoCol_Inactive] = ImVec4{ 0.6f, 0.6f, 0.6f, 0.6f };
  colors[ImGuizmoCol_Hovered] = ImVec4{ 1.0f, 0.5f, 0.06f, 1.0f };

  colors[ImGuizmoCol_SpecialMove] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };

  colors[ImGuizmoCol_AxisX] = ImVec4{ 1.0f, 0.2f, 0.321f, 1.0f };
  colors[ImGuizmoCol_AxisY] = ImVec4{ 0.545f, 0.862f, 0.0f, 1.0f };
  colors[ImGuizmoCol_AxisZ] = ImVec4{ 0.156f, 0.564f, 1.0f, 1.0f };

  colors[ImGuizmoCol_PlaneYZ] = ImVec4{ 1.0f, 0.2f, 0.321f, 0.6f };
  colors[ImGuizmoCol_PlaneZX] = ImVec4{ 0.545f, 0.862f, 0.0f, 0.6f };
  colors[ImGuizmoCol_PlaneXY] = ImVec4{ 0.156f, 0.564f, 1.0f, 0.6f };

  colors[ImGuizmoCol_BoundAnchor] = ImVec4{ 0.66f, 0.66f, 0.66f, 1.0f };
}
void StyleColorsUnreal(ImGuizmoStyle *dst) {
  ImGuizmoStyle *style{ dst ? dst : &ImGuizmo::GetStyle() };
  ImVec4 *colors{ style->Colors };

  colors[ImGuizmoCol_Text] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };
  colors[ImGuizmoCol_TextShadow] = ImVec4{ 0.0f, 0.0f, 0.0f, 1.0f };

  colors[ImGuizmoCol_Inactive] = ImVec4{ 0.7f, 0.7f, 0.7f, 0.7f };
  colors[ImGuizmoCol_Hovered] = ImVec4{ 1.0f, 1.0f, 0.0f, 1.0f };

  colors[ImGuizmoCol_SpecialMove] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };

  colors[ImGuizmoCol_AxisX] = ImVec4{ 0.594f, 0.0197f, 0.0f, 1.0f };
  colors[ImGuizmoCol_AxisY] = ImVec4{ 0.1349f, 0.3959f, 0.0f, 1.0f };
  colors[ImGuizmoCol_AxisZ] = ImVec4{ 0.0251f, 0.207f, 0.85f, 1.0f };

  colors[ImGuizmoCol_PlaneYZ] = ImVec4{ 0.594f, 0.0197f, 0.0f, 0.6f };
  colors[ImGuizmoCol_PlaneZX] = ImVec4{ 0.1349f, 0.3959f, 0.0f, 0.6f };
  colors[ImGuizmoCol_PlaneXY] = ImVec4{ 0.0251f, 0.207f, 0.85f, 0.6f };

  colors[ImGuizmoCol_BoundAnchor] = ImVec4{ 0.66f, 0.66f, 0.66f, 1.0f };

  constexpr float gamma{ 2.2f };
  for (int i = 0; i < ImGuizmoCol_COUNT; ++i) {
    const glm::vec4 &src_color{ colors[i] };
    colors[i] = glm::vec4{ glm::pow(src_color.rgb(), glm::vec3{ 1.0f / gamma }),
                           src_color.a };
  }
}

static const char *GetStyleColorName(ImGuizmoCol idx) {
  switch (idx) {
  case ImGuizmoCol_Text: return "Text";
  case ImGuizmoCol_TextShadow: return "TextShadow";
  case ImGuizmoCol_Inactive: return "Inactive";
  case ImGuizmoCol_Hovered: return "Hovered";
  case ImGuizmoCol_SpecialMove: return "SpecialMove";
  case ImGuizmoCol_AxisX: return "AxisX";
  case ImGuizmoCol_AxisY: return "AxisY";
  case ImGuizmoCol_AxisZ: return "AxisZ";
  case ImGuizmoCol_PlaneYZ: return "PlaneYZ";
  case ImGuizmoCol_PlaneZX: return "PlaneZX";
  case ImGuizmoCol_PlaneXY: return "PlaneXY";
  case ImGuizmoCol_BoundAnchor: return "BoundAnchor";
  }
  IM_ASSERT(0);
  return "Unknown";
}
void ShowStyleEditor(ImGuizmoStyle *ref) {
  ImGuizmoStyle &style{ ImGuizmo::GetStyle() };
  static ImGuizmoStyle ref_saved_style;

  static bool init{ true };
  if (init && ref == nullptr) ref_saved_style = style;
  init = false;
  if (ref == nullptr) ref = &ref_saved_style;

  // @todo ...

  ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f);

  if (ImGuizmo::ShowStyleSelector("Colors##Selector")) ref_saved_style = style;

  ImGui::DragFloat("Alpha", &style.Alpha, 0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("GizmoScale", &style.GizmoScale, 0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("RingThickness", &style.RotationRingThickness, 0.1f, 0.1f,
                   10.0f);

  static ImGuiColorEditFlags alpha_flags{
    ImGuiColorEditFlags_AlphaPreviewHalf
  };
  for (int i = 0; i < ImGuizmoCol_COUNT; ++i) {
    const char *name{ ImGuizmo::GetStyleColorName(i) };

    ImGui::PushID(i);
    ImGui::ColorEdit4("##color", (float *)&style.Colors[i],
                      ImGuiColorEditFlags_AlphaBar | alpha_flags);
    ImGui::SameLine();
    ImGui::TextUnformatted(name);
    ImGui::PopID();
  }
}
bool ShowStyleSelector(const char *label) {
  static int style_idx{ -1 };
  if (ImGui::Combo(label, &style_idx, "Classic\0Blender\0Unreal\0")) {
    switch (style_idx) {
    case 0:
      StyleColorsClassic();
      break;
    case 1:
      StyleColorsBlender();
      break;
    case 2:
      StyleColorsUnreal();
      break;
    }
    return true;
  }
  return false;
}

static ImU32 GetColorU32(ImGuizmoCol idx, float alpha_mul = 1.0f) {
  const ImGuizmoStyle &style{ GImGuizmo.Style };
  ImVec4 c{ style.Colors[idx] };
  c.w *= style.Alpha * alpha_mul;
  return ImGui::ColorConvertFloat4ToU32(c);
}
static const ImVec4 &GetStyleColorVec4(ImGuizmoCol idx) {
  const ImGuizmoStyle &style{ GImGuizmo.Style };
  return style.Colors[idx];
}

//-----------------------------------------------------------------------------
// [SECTION] MISC HELPERS/UTILITIES (Geometry functions)
//-----------------------------------------------------------------------------

static glm::vec2 WorldToScreen(const glm::vec3 &world_pos,
                               const glm::mat4 &matrix, const ImRect &bb) {
  glm::vec4 temp{ matrix * glm::vec4{ world_pos, 1.0f } };
  temp *= 0.5f / temp.w;

  ImVec2 screen_pos{ temp.xy() + 0.5f };
  screen_pos.y = 1.0f - screen_pos.y;
  screen_pos *= bb.GetSize();
  screen_pos += bb.GetTL();
  return screen_pos;
}
static glm::vec2 WorldToScreen(const glm::vec3 &world_pos,
                               const glm::mat4 &matrix) {
  return WorldToScreen(world_pos, matrix, GImGuizmo.Viewport);
}

static ImGuizmoRay RayCast(const glm::mat4 &view_proj_matrix,
                           const ImRect &bb) {
  glm::vec2 mouse_pos{ ImGui::GetIO().MousePos };
  // Convert to NDC
  mouse_pos =
    glm::vec2{ (mouse_pos - bb.GetTL()) / bb.GetSize() } * 2.0f - 1.0f;
  mouse_pos.y *= -1.0f;

  const glm::mat4 inversed_view_proj{ glm::inverse(view_proj_matrix) };
  glm::vec4 ray_origin_world_space{ inversed_view_proj *
                                    glm::vec4{ mouse_pos, 0.0f, 1.0f } };
  ray_origin_world_space *= 1.0f / ray_origin_world_space.w;
  glm::vec4 ray_end_world_space{
    inversed_view_proj * glm::vec4{ mouse_pos, 1.0f - kEpsilon, 1.0f }
  };
  ray_end_world_space *= 1.0f / ray_end_world_space.w;
  return ImGuizmoRay{ ray_origin_world_space, ray_end_world_space,
                      glm::normalize(ray_end_world_space -
                                     ray_origin_world_space) };
}
static ImGuizmoRay RayCast(const glm::mat4 &view_proj_matrix) {
  return RayCast(view_proj_matrix, GImGuizmo.Viewport);
}

static glm::vec4 BuildPlane(const glm::vec3 &point, const glm::vec3 &normal) {
  const glm::vec3 n{ glm::normalize(normal) };
  return glm::vec4{ n, glm::dot(n, point) };
}
static float DistanceToPlane(const glm::vec3 &point, const glm::vec4 &plane) {
  return glm::dot(plane.xyz(), point) + plane.w;
}
static float IntersectRayPlane(const ImGuizmoRay &ray, const glm::vec4 &plane) {
  const float num{ glm::dot(plane.xyz(), ray.Origin) - plane.w };
  const float denom{ glm::dot(plane.xyz(), ray.Direction) };
  // Normal is orthogonal to vector, can't intersect
  if (glm::abs(denom) < kEpsilon) return -1.0f;
  return -(num / denom);
}

static glm::vec2 PointOnSegment(const glm::vec2 &point, const glm::vec2 &v1,
                                const glm::vec2 &v2) {
  const glm::vec2 c{ point - v1 };
  const glm::vec2 V{ glm::normalize(v2 - v1) };
  const float t{ glm::dot(V, c) };
  if (t < 0.0f) return v1;
  const float d{ glm::length(v2 - v1) };
  if (t > d) return v2;

  return v1 + V * t;
}
static float GetSegmentLengthClipSpace(const glm::vec3 &start,
                                       const glm::vec3 &end) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  auto start_of_segment = gizmo->ModelViewProjMatrix * glm::vec4{ start, 1.0f };
  // Check for axis aligned with camera direction
  if (glm::abs(start_of_segment.w) > kEpsilon)
    start_of_segment *= 1.0f / start_of_segment.w;

  auto end_of_segment = gizmo->ModelViewProjMatrix * glm::vec4{ end, 1.0f };
  // Check for axis aligned with camera direction
  if (glm::abs(end_of_segment.w) > kEpsilon)
    end_of_segment *= 1.0f / end_of_segment.w;

  glm::vec2 clip_space_axis{ end_of_segment - start_of_segment };
  clip_space_axis.y /= g.GetAspectRatio();
  return glm::length(clip_space_axis);
}

//-----------------------------------------------------------------------------
// [SECTION] UTILITIES (SNAP)
//-----------------------------------------------------------------------------

static void CalculateSnap(float &value, float snap) {
  if (snap <= kEpsilon) return;

  const float modulo{ glm::fmod(value, snap) };
  const float modulo_ratio{ glm::abs(modulo) / snap };
  constexpr float snap_tension{ 0.5f };
  if (modulo_ratio < snap_tension) {
    value -= modulo;
  } else if (modulo_ratio > (1.0f - snap_tension)) {
    value = value - modulo + snap * ((value < 0.0f) ? -1.0f : 1.0f);
  }
}
static void CalculateSnap(glm::vec3 &value, const float *snap) {
  for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
    CalculateSnap(value[axis_idx], snap[axis_idx]);
}

//-----------------------------------------------------------------------------
// [SECTION] UTILITIES
//-----------------------------------------------------------------------------

static const char *StatAxisFlags(ImGuizmoAxisFlags flags) {
  switch (flags) {
  case ImGuizmoAxisFlags_None: return "None";
  case ImGuizmoAxisFlags_X: return "X";
  case ImGuizmoAxisFlags_Y: return "Y";
  case ImGuizmoAxisFlags_Z: return "Z";
  case ImGuizmoAxisFlags_YZ: return "YZ";
  case ImGuizmoAxisFlags_ZX: return "ZX";
  case ImGuizmoAxisFlags_XY: return "XY";
  case ImGuizmoAxisFlags_ALL: return "XYZ";
  default:
    IM_ASSERT(0);
    return "Unknown";
  }
}

static const char *GetAxisName(ImGuizmoAxis axis_idx) {
  switch (axis_idx) {
  case ImGuizmoAxis_X: return "X";
  case ImGuizmoAxis_Y: return "Y";
  case ImGuizmoAxis_Z: return "Z";
  default:
    IM_ASSERT(0);
    return "Unknown";
  }
}
static bool HasSingleAxis(ImGuizmoAxisFlags flags) {
  return flags == ImGuizmoAxisFlags_X || flags == ImGuizmoAxisFlags_Y ||
         flags == ImGuizmoAxisFlags_Z;
}
static ImGuizmoAxis GetAxisIdx(ImGuizmoAxisFlags flags, bool around) {
  switch (flags) {
  case ImGuizmoAxisFlags_X: return around ? ImGuizmoAxis_Z : ImGuizmoAxis_X;
  case ImGuizmoAxisFlags_Y: return ImGuizmoAxis_Y;
  case ImGuizmoAxisFlags_Z: return around ? ImGuizmoAxis_X : ImGuizmoAxis_Z;
  default:
    IM_ASSERT(0);
    return -1;
  }
}
static ImGuizmoAxis GetAxisAroundIdx(ImGuizmoAxis axis_idx) {
  switch (axis_idx) {
  case ImGuizmoAxis_X: return ImGuizmoAxis_Z;
  case ImGuizmoAxis_Y: return ImGuizmoAxis_Y;
  case ImGuizmoAxis_Z: return ImGuizmoAxis_X;
  default:
    IM_ASSERT(0);
    return -1;
  }
}
static ImGuizmoAxisFlags AxisToFlag(ImGuizmoAxis axis_idx, bool around) {
  switch (axis_idx) {
  case ImGuizmoAxis_X: return around ? ImGuizmoAxisFlags_Z : ImGuizmoAxisFlags_X;
  case ImGuizmoAxis_Y: return ImGuizmoAxisFlags_Y;
  case ImGuizmoAxis_Z: return around ? ImGuizmoAxisFlags_X : ImGuizmoAxisFlags_Z;
  default:
    IM_ASSERT(0);
    return -1;
  }
}

static const char *GetPlaneName(ImGuizmoPlane plane_idx) {
  switch (plane_idx) {
  case ImGuizmoPlane_YZ: return "YZ";
  case ImGuizmoPlane_ZX: return "ZX";
  case ImGuizmoPlane_XY: return "XY";
  default:
    IM_ASSERT(0);
    return "Unknown";
  }
}
static bool HasPlane(ImGuizmoAxisFlags flags) {
  if (flags == ImGuizmoAxisFlags_ALL) return false;
  return flags == ImGuizmoAxisFlags_YZ || flags == ImGuizmoAxisFlags_ZX ||
         flags == ImGuizmoAxisFlags_XY;
}
static ImGuizmoPlane GetPlaneIdx(ImGuizmoAxisFlags flags) {
  switch (flags) {
  case ImGuizmoAxisFlags_YZ: return ImGuizmoPlane_YZ;
  case ImGuizmoAxisFlags_ZX: return ImGuizmoPlane_ZX;
  case ImGuizmoAxisFlags_XY: return ImGuizmoPlane_XY;
  default:
    IM_ASSERT(0);
    return -1;
  }
}
static ImGuizmoAxisFlags PlaneToFlags(ImGuizmoPlane plane_idx) {
  switch (plane_idx) {
  case ImGuizmoPlane_YZ: return ImGuizmoAxisFlags_YZ;
  case ImGuizmoPlane_ZX: return ImGuizmoAxisFlags_ZX;
  case ImGuizmoPlane_XY: return ImGuizmoAxisFlags_XY;
  default:
    IM_ASSERT(0);
    return -1;
  }
}

//-----------------------------------------------------------------------------
// [SECTION]
//-----------------------------------------------------------------------------

static ImRect CalculateViewport() {
  const auto region_min = ImGui::GetWindowContentRegionMin();
  const auto region_max = ImGui::GetWindowContentRegionMax();
  const auto position = ImGui::GetWindowPos() + region_min;
  const auto size = region_max - region_min;
  return ImRect{ position, position + size };
}

static bool GizmoBehavior(ImGuizmoOperation operation,
                          ImGuizmoAxisFlags &hover_flags, bool *out_held) {
  IM_ASSERT(operation);

  const ImGuiIO &io{ ImGui::GetIO() };
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (g.ActiveGizmo != gizmo && g.ActiveGizmo) {
    hover_flags = ImGuizmoAxisFlags_None;
  } else {
    if (gizmo->ActiveOperation != operation && gizmo->ActiveOperation) {
      hover_flags = ImGuizmoAxisFlags_None;
    } else {
      if (gizmo->ActiveManipulationFlags)
        hover_flags = gizmo->ActiveManipulationFlags;
    }
  }

  bool pressed{ hover_flags && io.MouseClicked[0] };
  if (pressed) {
    g.ActiveGizmo = gizmo;
    gizmo->ActiveOperation = operation;
    gizmo->ActiveManipulationFlags = hover_flags;
  }

  bool held{ false };
  if (gizmo->ActiveManipulationFlags == hover_flags &&
      gizmo->ActiveManipulationFlags) {
    if (io.MouseDown[0]) {
      held = true;
    } else {
      g.ActiveGizmo = nullptr;
      gizmo->ActiveManipulationFlags = ImGuizmoAxisFlags_None;
    }
  }

  if (out_held) *out_held = held;
  return pressed;
}
static bool BoundBehavior(ImGuizmoAxisFlags &hover_flags,
                          ImGuizmoPlane &hovered_plane_idx,
                          int &hovered_bound_idx, bool *out_held) {
  ImGuizmoContext &g{ GImGuizmo };

  bool held;
  auto pressed =
    GizmoBehavior(ImGuizmoOperation_BoundsScale, hover_flags, &held);

  if (pressed) {
    g.Bounds.ActivePlane = hovered_plane_idx;
    g.Bounds.ActiveBoundIdx = hovered_bound_idx;
  }
  if (held) {
    hovered_plane_idx = g.Bounds.ActivePlane;
    hovered_bound_idx = g.Bounds.ActiveBoundIdx;
  }

  if (out_held) *out_held = held;
  return pressed;
}

static bool ViewManipulatorBehavior(bool hovered, bool *out_held) {
  const ImGuiIO &io{ ImGui::GetIO() };

  static bool active{ false };
  if (!io.MouseDown[1]) active = false;

  bool pressed{ hovered && (io.MouseClicked[0] || io.MouseClicked[1]) };
  if (pressed) active = true;

  bool held{ active && io.MouseDown[1] };

  if (out_held) *out_held = held;
  return pressed;
}

//-----------------------------------------------------------------------------
// [SECTION] HOVER QUERY
//-----------------------------------------------------------------------------

static bool CanActivate() {
  return ImGui::IsWindowHovered() &&
         GImGuizmo.Viewport.Contains(ImGui::GetIO().MousePos);
}

static bool IsCoreHovered() {
  const ImGuiIO &io{ ImGui::GetIO() };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (gizmo->LockedAxesFlags == ImGuizmoAxisFlags_ALL) return false;

  constexpr float tolerance{ 3.0f };
  const float distance{ glm::length(glm::vec2{ io.MousePos } - gizmo->Origin) };
  return distance <= kCircleRadius + tolerance;
}

static bool IsAxisHovered(ImGuizmoAxis axis_idx) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (gizmo->LockedAxesFlags & AxisToFlag(axis_idx, false)) return false;

  const glm::vec3 dir_axis{ gizmo->ModelMatrix *
                            glm::vec4{ kUnitDirections[axis_idx], 0.0f } };
  const float length{ IntersectRayPlane(
    g.Ray, BuildPlane(gizmo->ModelMatrix[3], dir_axis)) };
  const glm::vec2 mouse_pos_on_plane{ WorldToScreen(
    g.Ray.Origin + g.Ray.Direction * length, g.Camera.ViewProjectionMatrix) };

  constexpr float axis_shift{ 0.1f };
  const glm::vec2 axis_start_on_screen{ WorldToScreen(
    gizmo->ModelMatrix[3].xyz() + dir_axis * gizmo->ScreenFactor * axis_shift,
    g.Camera.ViewProjectionMatrix) };
  const glm::vec2 axis_end_on_screen{ WorldToScreen(
    gizmo->ModelMatrix[3].xyz() + dir_axis * gizmo->ScreenFactor,
    g.Camera.ViewProjectionMatrix) };
  const glm::vec2 closest_point_on_axis{ PointOnSegment(
    mouse_pos_on_plane, axis_start_on_screen, axis_end_on_screen) };
  constexpr float tolerance{ 6.0f };
  return glm::length(closest_point_on_axis - mouse_pos_on_plane) < tolerance;
}
static bool IsPlaneHovered(ImGuizmoPlane plane_idx) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (gizmo->LockedAxesFlags & PlaneToFlags(plane_idx)) return false;

  const glm::vec3 plane_normal{ gizmo->ModelMatrix *
                                glm::vec4{ kUnitDirections[plane_idx], 0.0f } };
  const float length{ IntersectRayPlane(
    g.Ray, BuildPlane(gizmo->ModelMatrix[3], plane_normal)) };
  const glm::vec3 mouse_pos_on_plane{ g.Ray.Origin + g.Ray.Direction * length };

  const glm::vec3 plane_dir1{
    gizmo->ModelMatrix * glm::vec4{ kUnitDirections[(plane_idx + 1) % 3], 0.0f }
  };
  const float dx{ glm::dot(plane_dir1,
                           (mouse_pos_on_plane - gizmo->ModelMatrix[3].xyz()) *
                             (1.0f / gizmo->ScreenFactor)) };
  const glm::vec3 plane_dir2{
    gizmo->ModelMatrix * glm::vec4{ kUnitDirections[(plane_idx + 2) % 3], 0.0f }
  };
  const float dy{ glm::dot(plane_dir2,
                           (mouse_pos_on_plane - gizmo->ModelMatrix[3].xyz()) *
                             (1.0f / gizmo->ScreenFactor)) };
  return (dx >= kUnitQuad[0] && dx <= kUnitQuad[4] && dy >= kUnitQuad[1] &&
          dy <= kUnitQuad[3]);
}

static bool IsRotationAxisHovered(ImGuizmoAxis axis_idx) {
  const ImGuiIO &io{ ImGui::GetIO() };
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (gizmo->LockedAxesFlags == AxisToFlag(axis_idx, false)) return false;

  const glm::vec4 pickup_plane{ BuildPlane(gizmo->ModelMatrix[3],
                                           gizmo->ModelMatrix[axis_idx]) };
  const float length{ IntersectRayPlane(g.Ray, pickup_plane) };
  const glm::vec3 local_pos{ glm::normalize(
    g.Ray.Origin + g.Ray.Direction * length - gizmo->ModelMatrix[3].xyz()) };

  if (glm::dot(local_pos, g.Ray.Direction) > kEpsilon) return false;

  const glm::vec3 ideal_pos_on_circle{ gizmo->InversedModelMatrix *
                                       glm::vec4{ local_pos, 0.0f } };
  const glm::vec2 ideal_pos_on_circle_screen_space{ WorldToScreen(
    ideal_pos_on_circle * gizmo->ScreenFactor, gizmo->ModelViewProjMatrix) };

  constexpr float tolerance{ 8.0f };
  const glm::vec2 distance_on_screen{ ideal_pos_on_circle_screen_space -
                                      io.MousePos };
  return glm::length(distance_on_screen) < tolerance;
}
static bool IsRotationRingHovered() {
  const ImGuiIO &io{ ImGui::GetIO() };
  const ImGuizmoStyle &style{ GetStyle() };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (gizmo->LockedAxesFlags == ImGuizmoAxisFlags_ALL) return false;

  constexpr float tolerance{ 1.0f };
  const float ring_thickness{ style.RotationRingThickness + tolerance };
  const float distance{ glm::length(glm::vec2{ io.MousePos } - gizmo->Origin) };
  return (distance >= gizmo->RingRadius - ring_thickness) &&
         (distance < gizmo->RingRadius + ring_thickness);
}

//-----------------------------------------------------------------------------
// [SECTION]
//-----------------------------------------------------------------------------

static bool IsAxisVisible(ImGuizmoAxis axis_idx) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  constexpr float visibility_threshold{ 0.03f };
  const float axis_length{ GetSegmentLengthClipSpace(
    glm::vec3{ 0.0f }, kUnitDirections[axis_idx] * gizmo->ScreenFactor) };
  return axis_length >= visibility_threshold;
}
static bool IsPlaneVisible(ImGuizmoPlane plane_idx) {
  constexpr float visibility_threshold{ 0.1f };
  return GImGuizmo.PlanesVisibility[plane_idx] >= visibility_threshold;
}

//-----------------------------------------------------------------------------
// [SECTION] RENDERING
//-----------------------------------------------------------------------------

// COLOR:

static ImU32 GetSpecialMoveColor(ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  ImU32 color{ GetColorU32(ImGuizmoCol_SpecialMove) };
  if (gizmo->LockedAxesFlags == ImGuizmoAxisFlags_ALL)
    color = GetColorU32(ImGuizmoCol_Inactive);
  else if (hover_flags == ImGuizmoAxisFlags_ALL)
    color = GetColorU32(ImGuizmoCol_Hovered, 0.541f);

  return color;
}
static ImU32 GetAxisColor(ImGuizmoAxis axis_idx, ImGuizmoAxisFlags hover_flags,
                          bool around) {
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  ImU32 color{ GetColorU32(ImGuizmoCol_AxisX +
                           (around ? GetAxisAroundIdx(axis_idx) : axis_idx)) };
  if (gizmo->LockedAxesFlags & AxisToFlag(axis_idx, around)) {
    color = GetColorU32(ImGuizmoCol_Inactive);
  } else if (HasSingleAxis(hover_flags) &&
             GetAxisIdx(hover_flags, around) == axis_idx) {
    color = GetColorU32(ImGuizmoCol_Hovered, 0.541f);
  }
  return color;
}
static ImU32 GetPlaneColor(ImGuizmoPlane plane_idx,
                           ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  ImU32 color{ GetColorU32(ImGuizmoCol_PlaneYZ + plane_idx) };
  if (gizmo->LockedAxesFlags & PlaneToFlags(plane_idx))
    color = GetColorU32(ImGuizmoCol_Inactive);
  else if (HasPlane(hover_flags) && GetPlaneIdx(hover_flags) == plane_idx)
    color = GetColorU32(ImGuizmoCol_Hovered, 0.541f);

  return color;
}
static ImU32 GetBoundColor(bool hovered) {
  return hovered ? GetColorU32(ImGuizmoCol_Hovered, 0.541f)
                 : GetColorU32(ImGuizmoCol_BoundAnchor);
}

// TRANSLATION:

static void RenderCore(ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (g.ConfigFlags & ImGuizmoConfigFlags_HideLocked &&
      gizmo->LockedAxesFlags == ImGuizmoAxisFlags_ALL)
    return;

  const ImU32 color{ GetSpecialMoveColor(hover_flags) };
  g.DrawList->AddCircleFilled(gizmo->Origin, kCircleRadius, color,
                              kCircleSegmentCount);
}

static void RenderArrowhead(const glm::vec2 &head_pos, ImU32 color) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  constexpr float arrowhead_size{ kLineThickness * 2.0f };
  const glm::vec2 dir{ glm::normalize(gizmo->Origin - head_pos) *
                       arrowhead_size };
  const glm::vec2 orthogonal_dir{ dir.y, -dir.x };
  const glm::vec2 a{ head_pos + dir };
  g.DrawList->AddTriangleFilled(head_pos - dir, a + orthogonal_dir,
                                a - orthogonal_dir, color);
}
static void RenderTranslateAxis(ImGuizmoAxis axis_idx,
                                ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (!IsAxisVisible(axis_idx)) return;
  if (g.ConfigFlags & ImGuizmoConfigFlags_HideLocked &&
      gizmo->LockedAxesFlags & AxisToFlag(axis_idx, false))
    return;

  const glm::vec3 &dir_axis{ kUnitDirections[axis_idx] };
  const glm::vec2 tail_pos{ WorldToScreen(dir_axis * 0.1f * gizmo->ScreenFactor,
                                          gizmo->ModelViewProjMatrix) };
  const glm::vec2 head_pos{ WorldToScreen(dir_axis * gizmo->ScreenFactor,
                                          gizmo->ModelViewProjMatrix) };

  const ImU32 color{ GetAxisColor(axis_idx, hover_flags, false) };
  g.DrawList->AddLine(tail_pos, head_pos, color, kLineThickness);
  RenderArrowhead(head_pos, color);
}
static void RenderPlane(ImGuizmoPlane plane_idx,
                        ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  // @todo Or maybe multiply alpha by PlaneVisibility? (blender-like behavior)
  if (!IsPlaneVisible(plane_idx)) return;

  if (g.ConfigFlags & ImGuizmoConfigFlags_HideLocked &&
      gizmo->LockedAxesFlags & PlaneToFlags(plane_idx))
    return;

  ImVec2 plane_points[4];
  for (int i = 0; i < 4; ++i) {
    const glm::vec3 corner_world_space{
      (kUnitDirections[(plane_idx + 1) % 3] * kUnitQuad[i * 2] +
       kUnitDirections[(plane_idx + 2) % 3] * kUnitQuad[i * 2 + 1]) *
      gizmo->ScreenFactor
    };
    plane_points[i] =
      WorldToScreen(corner_world_space, gizmo->ModelViewProjMatrix);
  }

  const ImU32 color{ GetPlaneColor(plane_idx, hover_flags) };
  g.DrawList->AddConvexPolyFilled(plane_points, 4, color);
  constexpr float plane_border{ 1.5f };
  g.DrawList->AddPolyline(plane_points, 4, color | 0x60000000, true,
                          plane_border);
}
static void RenderTranslationTrail() {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const glm::vec2 tail_pos{ WorldToScreen(gizmo->DragTranslationOrigin,
                                          g.Camera.ViewProjectionMatrix) };
  const glm::vec2 head_pos{ WorldToScreen(gizmo->ModelMatrix[3],
                                          g.Camera.ViewProjectionMatrix) };
  const glm::vec2 diff{ glm::normalize(head_pos - tail_pos) *
                        (kCircleRadius - 1.0f) };

  constexpr ImU32 trail_line_color{ 0xAAAAAAAA };
  constexpr float margin{ 1.5f };
  g.DrawList->AddCircle(tail_pos, kCircleRadius + margin, trail_line_color);
  g.DrawList->AddCircle(head_pos, kCircleRadius + margin, trail_line_color);
  g.DrawList->AddLine(tail_pos + diff, head_pos - diff, trail_line_color,
                      kLineThickness / 2);
}

// ROTATION:

static void RenderRotationAxis(ImGuizmoAxis axis_idx, bool circle,
                               ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (g.ConfigFlags & ImGuizmoConfigFlags_HideLocked &&
      gizmo->LockedAxesFlags & AxisToFlag(axis_idx, true))
    return;

  glm::vec3 camera_to_model_normalized =
    g.Camera.IsOrtho
      ? -glm::inverse(g.Camera.ViewMatrix)[2]
      : glm::normalize(gizmo->ModelMatrix[3].xyz() - g.Camera.Eye);
  camera_to_model_normalized =
    gizmo->InversedModelMatrix * glm::vec4{ camera_to_model_normalized, 0.0f };

  const float angle_start{ (glm::atan(
                             camera_to_model_normalized[(4 - axis_idx) % 3],
                             camera_to_model_normalized[(3 - axis_idx) % 3])) +
                           kPi * 0.5f };
  ImVec2 circle_pos[kCircleSegmentCount];
  for (int i = 0; i < kCircleSegmentCount; ++i) {
    const float ng{ angle_start +
                    (circle ? 2 : 1) * kPi *
                      (static_cast<float>(i) / kCircleSegmentCount) };
    const glm::vec3 axis_pos{ glm::cos(ng), glm::sin(ng), 0.0f };
    const auto pos =
      glm::vec3{ axis_pos[axis_idx], axis_pos[(axis_idx + 1) % 3],
                 axis_pos[(axis_idx + 2) % 3] } *
      gizmo->ScreenFactor;
    circle_pos[i] = WorldToScreen(pos, gizmo->ModelViewProjMatrix);
  }

  const ImU32 color{ GetAxisColor(axis_idx, hover_flags, true) };
  g.DrawList->AddPolyline(circle_pos, kCircleSegmentCount, color, circle,
                          kLineThickness);
}
static void RenderRotationRing(ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (g.ConfigFlags & ImGuizmoConfigFlags_HideLocked &&
      gizmo->LockedAxesFlags == ImGuizmoAxisFlags_ALL)
    return;

  const ImU32 color{ GetSpecialMoveColor(hover_flags) };
  g.DrawList->AddCircle(gizmo->Origin, gizmo->RingRadius, color,
                        kCircleSegmentCount, g.Style.RotationRingThickness);
}
static void RenderRotationTrail() {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  ImU32 border_color, color;
#if 1
  border_color = GetColorU32(ImGuizmoCol_Hovered);
  color = GetColorU32(ImGuizmoCol_Hovered, 0.541f);
#else
  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  if (HasSingleAxis(hover_flags)) {
    const ImGuizmoAxis axis_idx{ GetAxisIdx(hover_flags, false) };
    color = GetColorU32(ImGuizmoCol_AxisX + axis_idx, 0.541f);
    border_color = GetColorU32(ImGuizmoCol_AxisX + axis_idx);
  } else {
    color = GetColorU32(ImGuizmoCol_SpecialMove, 0.541f);
    border_color = GetColorU32(ImGuizmoCol_SpecialMove);
  }
#endif

  ImVec2 circle_points[kCircleSegmentCount + 1]{ gizmo->Origin };
  for (int i = 1; i < kCircleSegmentCount; ++i) {
    const float ng{ gizmo->RotationAngle *
                    (static_cast<float>(i - 1) / (kCircleSegmentCount - 1)) };
    const glm::mat4 rotate_vector_matrix{ glm::rotate(
      ng, gizmo->TranslationPlane.xyz()) };
    glm::vec3 pos{ rotate_vector_matrix *
                   glm::vec4{ gizmo->RotationVectorSource, 1.0f } };
    pos *= gizmo->ScreenFactor;
    circle_points[i] = WorldToScreen(pos + gizmo->ModelMatrix[3].xyz(),
                                     g.Camera.ViewProjectionMatrix);
  }

  g.DrawList->AddConvexPolyFilled(circle_points, kCircleSegmentCount, color);
  g.DrawList->AddPolyline(circle_points, kCircleSegmentCount, border_color,
                          true, kLineThickness);
}

// SCALE:

static void RenderScaleAxis(ImGuizmoAxis axis_idx,
                            ImGuizmoAxisFlags hover_flags) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  if (!IsAxisVisible(axis_idx)) return;
  if (g.ConfigFlags & ImGuizmoConfigFlags_HideLocked &&
      gizmo->LockedAxesFlags & AxisToFlag(axis_idx, false))
    return;

  const glm::vec3 &dir_axis{ kUnitDirections[axis_idx] };
  const glm::vec2 tail_pos{ WorldToScreen(dir_axis * 0.1f * gizmo->ScreenFactor,
                                          gizmo->ModelViewProjMatrix) };
  const glm::vec2 head_pos{ WorldToScreen(dir_axis * gizmo->ScreenFactor,
                                          gizmo->ModelViewProjMatrix) };

  const ImU32 color{ GetAxisColor(axis_idx, hover_flags, false) };
  g.DrawList->AddLine(tail_pos, head_pos, color, kLineThickness);
#if 1
  g.DrawList->AddCircleFilled(head_pos, kCircleRadius, color);
#else
  const glm::vec2 head_scaled{ WorldToScreen(
    (dir_axis * gizmo->Scale[axis_idx]) * gizmo->ScreenFactor,
    gizmo->ModelViewProjMatrix) };

  constexpr float kQuadSize{ kLineThickness * 2.0f };
  const glm::vec2 dir{ glm::normalize(gizmo->Origin - head_scaled) *
                       kQuadSize };
  const glm::vec2 a{ head_scaled + dir }, b{ head_scaled - dir };
  const ImVec2 points[4]{ a + glm::vec2{ dir.y, -dir.x },
                          a - glm::vec2{ dir.y, -dir.x },
                          b + glm::vec2{ -dir.y, dir.x },
                          b - glm::vec2{ -dir.y, dir.x } };
  g.DrawList->AddConvexPolyFilled(points, 4, color);
#endif
}
static void RenderScaleTrail(ImGuizmoAxis axis_idx) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const glm::vec2 head_pos{ WorldToScreen(
    kUnitDirections[axis_idx] * gizmo->Scale[axis_idx] * gizmo->ScreenFactor,
    gizmo->ModelViewProjMatrix) };
  g.DrawList->AddCircleFilled(head_pos, kCircleRadius, 0xFFFFFFFF);
}

// BOUNDS:

static void RenderDottedLine(const glm::vec2 &point_a, const glm::vec2 &point_b,
                             ImU32 color) {
  ImGuizmoContext &g{ GImGuizmo };

  const float distance{ glm::distance(point_a, point_b) };
  auto step_count = glm::min(static_cast<int>(distance / 15.0f), 1000);
  const float step_length{ 1.0f / step_count };
  for (int i = 0; i < step_count; ++i) {
    const float t1{ static_cast<float>(i) * step_length };
    const glm::vec2 tail_pos{ glm::lerp(point_a, point_b, t1) };
    const float t2{ t1 + step_length * 0.5f };
    const glm::vec2 head_pos{ glm::lerp(point_a, point_b, t2) };
    g.DrawList->AddLine(tail_pos, head_pos, color, 1.0f);
  }
}
static void RenderAnchor(const glm::vec2 &pos, float radius, ImU32 color) {
  const ImGuizmoContext &g{ GImGuizmo };
  constexpr float border{ 1.2f };
  g.DrawList->AddCircleFilled(pos, radius, 0xFF000000);
  g.DrawList->AddCircleFilled(pos, radius - border, color);
}
static void RenderBounds(const glm::mat4 &model_view_proj,
                         ImGuizmoAxisFlags hover_flags, int hovered_plane_idx,
                         int hovered_bound_idx) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  // Bounds are rendered starting from least visible plane. This is necessary in
  // case that there are common 'mid-points' between planes so we can avoid
  // drawing inactive bound over active one.
  for (int i = 2; i >= 0; --i) {
    ImGuizmoPlane plane_idx{ g.MostVisiblePlanes[i] };
    if (gizmo->ActiveOperation == ImGuizmoOperation_BoundsScale &&
        gizmo->ActiveManipulationFlags && hovered_plane_idx != plane_idx)
      continue;
    if (!IsPlaneVisible(plane_idx)) continue;

    const glm::vec3 *outter_points{ g.Bounds.OutterPoints[plane_idx] };
    const glm::vec3 *mid_points{ g.Bounds.MidPoints[plane_idx] };
    for (int i = 0; i < 4; ++i) {
      const glm::vec2 point{ WorldToScreen(outter_points[i], model_view_proj) };
      const glm::vec2 next_point{ WorldToScreen(outter_points[(i + 1) % 4],
                                                model_view_proj) };
      RenderDottedLine(point, next_point, 0xAAAAAAAA);

      const bool pre_match{ plane_idx == hovered_plane_idx &&
                            i == hovered_bound_idx };
      const bool outter_bound_hovered{ pre_match && HasPlane(hover_flags) };
      RenderAnchor(point, kOutterAnchorSize,
                   GetBoundColor(outter_bound_hovered));

      const glm::vec2 mid_point{ WorldToScreen(mid_points[i],
                                               model_view_proj) };
      const bool mid_bound_hovered{ pre_match && HasSingleAxis(hover_flags) };
      RenderAnchor(mid_point, kMidAnchorSize, GetBoundColor(mid_bound_hovered));
    }
  }
}

// TEXT:

static void RenderText(const glm::vec2 &position, const char *text) {
  const ImGuizmoContext &g{ GImGuizmo };
  g.DrawList->AddText(position + 15.0f, GetColorU32(ImGuizmoCol_TextShadow),
                      text);
  g.DrawList->AddText(position + 14.0f, GetColorU32(ImGuizmoCol_Text), text);
}

static const char *kInfoMasks[]{
  // -- Translation:
  "X : %5.3f",                     // 0
  "Y : %5.3f",                     // 1
  "Z : %5.3f",                     // 2
  "Y : %5.3f Z : %5.3f",           // 3
  "X : %5.3f Z : %5.3f",           // 6
  "X : %5.3f Y : %5.3f",           // 9
  "X : %5.3f Y : %5.3f Z : %5.3f", // 0

  // -- Rotation:
  "X : %5.2f deg %5.2f rad",      // 0
  "Y : %5.2f deg %5.2f rad",      // 1
  "Z : %5.2f deg %5.2f rad",      // 2
  "Screen : %5.2f deg %5.2f rad", // 0

  // -- Scale
  "XYZ : %5.2f" // 0
};
static const int kInfoDataIndices[]{
  0, 1, 2, // XYZ
  1, 2, 0, // YZ (0-unused)
  0, 2, 0, // XZ (0-unused)
  0, 1, 0, // XY (0-unused)
};

static void RenderTranslationInfo() {
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  const char *mask{ nullptr };
  int start_idx{ 0 };
  if (HasSingleAxis(hover_flags)) {
    const ImGuizmoAxis axis_idx{ GetAxisIdx(hover_flags, false) };
    mask = kInfoMasks[axis_idx];
    start_idx = axis_idx;
  } else if (HasPlane(hover_flags)) {
    const ImGuizmoPlane plane_idx{ GetPlaneIdx(hover_flags) };
    mask = kInfoMasks[ImGuizmoAxis_COUNT + plane_idx];
    start_idx = ImGuizmoAxis_COUNT + (ImGuizmoPlane_COUNT * plane_idx);
  } else {
    mask = kInfoMasks[ImGuizmoAxis_COUNT + ImGuizmoPlane_COUNT];
  }

  const glm::vec3 delta_info{ gizmo->ModelMatrix[3].xyz() -
                              gizmo->DragTranslationOrigin };

  char info_buffer[128]{};
  ImFormatString(info_buffer, sizeof(info_buffer), mask,
                 delta_info[kInfoDataIndices[start_idx]],
                 delta_info[kInfoDataIndices[start_idx + 1]],
                 delta_info[kInfoDataIndices[start_idx + 2]]);
  RenderText(gizmo->Origin, info_buffer);
}
static void RenderRotationInfo() {
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  const char *mask{ nullptr };
  if (HasSingleAxis(hover_flags)) {
    mask = kInfoMasks[ImGuizmoAxis_COUNT + ImGuizmoPlane_COUNT + 1 +
                      GetAxisIdx(hover_flags, false)];
  } else {
    mask = kInfoMasks[(ImGuizmoAxis_COUNT * 2) + ImGuizmoPlane_COUNT];
  }

  char info_buffer[128]{};
  ImFormatString(info_buffer, sizeof(info_buffer), mask,
                 glm::degrees(gizmo->RotationAngle), gizmo->RotationAngle);
  RenderText(gizmo->Origin, info_buffer);
}
static void RenderScaleInfo(const glm::vec3 &scale) {
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  const char *mask{ nullptr };
  int start_idx{ 0 };
  if (HasSingleAxis(hover_flags)) {
    const ImGuizmoAxis axis_idx{ GetAxisIdx(hover_flags, false) };
    mask = kInfoMasks[axis_idx];
    start_idx = axis_idx;
  } else {
    if (glm::all(glm::equal(scale, glm::vec3{ scale.x })))
      mask = kInfoMasks[11];
    else
      mask = kInfoMasks[6];
  }

  char info_buffer[128]{};
  ImFormatString(info_buffer, sizeof(info_buffer), mask,
                 scale[kInfoDataIndices[start_idx]],
                 scale[kInfoDataIndices[start_idx + 1]],
                 scale[kInfoDataIndices[start_idx + 2]]);
  RenderText(gizmo->Origin, info_buffer);
}

//-----------------------------------------------------------------------------
// [SECTION] TRANSLATION
//-----------------------------------------------------------------------------

static ImGuizmoAxisFlags FindTranslationHover() {
  if (!CanActivate()) return ImGuizmoAxisFlags_None;

  ImGuizmoAxisFlags hover_flags{ ImGuizmoAxisFlags_None };
  if (IsCoreHovered()) hover_flags |= ImGuizmoAxisFlags_ALL;
  if (hover_flags != ImGuizmoAxisFlags_ALL) {
    for (int plane_idx = 0; plane_idx < 3; ++plane_idx)
      if (IsPlaneHovered(plane_idx)) {
        hover_flags |= PlaneToFlags(plane_idx);
        break;
      }
    if (!HasPlane(hover_flags)) {
      for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
        if (IsAxisHovered(axis_idx)) {
          hover_flags |= AxisToFlag(axis_idx, false);
          break;
        }
    }
  }
  return hover_flags;
}
static glm::vec4 BuildTranslatePlane() {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };

  glm::vec3 move_plane_normal;
  if (HasPlane(hover_flags)) {
    move_plane_normal = gizmo->ModelMatrix[GetPlaneIdx(hover_flags)];
  } else if (HasSingleAxis(hover_flags)) {
    const glm::vec3 dir{ gizmo->ModelMatrix[GetAxisIdx(hover_flags, false)] };
    const glm::vec3 camera_to_model_normalized{ glm::normalize(
      gizmo->ModelMatrix[3].xyz() - g.Camera.Eye) };
    const glm::vec3 orthoDir{ glm::cross(dir, camera_to_model_normalized) };
    move_plane_normal = glm::normalize(glm::cross(dir, orthoDir));
  } else { // special movement
    move_plane_normal = -g.Camera.Forward;
  }
  return BuildPlane(gizmo->ModelMatrix[3], move_plane_normal);
}
static void BeginTranslation() {
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  g.BackupModelMatrix = gizmo->SourceModelMatrix;
  g.DragOrigin = ImGui::GetIO().MousePos;

  gizmo->DragTranslationOrigin = gizmo->ModelMatrix[3];
  gizmo->TranslationPlane = BuildTranslatePlane();
  const auto length{ IntersectRayPlane(g.Ray, gizmo->TranslationPlane) };
  gizmo->TranslationPlaneOrigin = g.Ray.Origin + g.Ray.Direction * length;
  gizmo->ModelRelativeOrigin =
    (gizmo->TranslationPlaneOrigin - gizmo->ModelMatrix[3].xyz()) *
    (1.0f / gizmo->ScreenFactor);
}
static void ContinueTranslation(const float *snap) {
  const ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };

  const float length{ glm::abs(
    IntersectRayPlane(g.Ray, gizmo->TranslationPlane)) };
  const glm::vec3 target_position{ g.Ray.Origin + g.Ray.Direction * length };
  const glm::vec3 new_position{ target_position - gizmo->ModelRelativeOrigin *
                                                    gizmo->ScreenFactor };

  glm::vec3 delta{ new_position - gizmo->ModelMatrix[3].xyz };
  if (HasSingleAxis(hover_flags)) {
    const ImGuizmoAxis axis_idx{ GetAxisIdx(hover_flags, false) };
    const glm::vec3 axis_value{ gizmo->ModelMatrix[axis_idx] };
    const float length_on_axis{ glm::dot(axis_value, delta) };
    delta = axis_value * length_on_axis;
  }

  if (snap) {
    glm::vec3 cumulative_delta{ gizmo->ModelMatrix[3].xyz + delta -
                                gizmo->DragTranslationOrigin };
    bool apply_rotation_localy{ gizmo->Mode == ImGuizmoMode_Local ||
                                hover_flags == ImGuizmoAxisFlags_ALL };
    if (apply_rotation_localy) {
      glm::mat4 source_model_normalized{ gizmo->SourceModelMatrix };
      for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
        source_model_normalized[axis_idx] =
          glm::normalize(source_model_normalized[axis_idx]);
      cumulative_delta = glm::inverse(source_model_normalized) *
                         glm::vec4{ cumulative_delta, 0.0f };
      CalculateSnap(cumulative_delta, snap);
      cumulative_delta =
        source_model_normalized * glm::vec4{ cumulative_delta, 0.0f };
    } else {
      CalculateSnap(cumulative_delta, snap);
    }
    delta = gizmo->DragTranslationOrigin + cumulative_delta -
            gizmo->ModelMatrix[3].xyz;
  }

  if (delta != gizmo->LastTranslationDelta) {
    gizmo->ModelMatrix = glm::translate(delta) * gizmo->SourceModelMatrix;
    for (int axis_idx = 0; axis_idx < 3; ++axis_idx) {
      if (gizmo->LockedAxesFlags & AxisToFlag(axis_idx, false))
        gizmo->ModelMatrix[3][axis_idx] =
          gizmo->DragTranslationOrigin[axis_idx];
    }
    gizmo->Dirty = true;
  }
  gizmo->LastTranslationDelta = delta;
}

//-----------------------------------------------------------------------------
// [SECTION] ROTATION
//-----------------------------------------------------------------------------

static ImGuizmoAxisFlags FindRotationHover() {
  if (!CanActivate()) return ImGuizmoAxisFlags_None;

  ImGuizmoAxisFlags hover_flags{ ImGuizmoAxisFlags_None };
  if (IsRotationRingHovered()) hover_flags |= ImGuizmoAxisFlags_ALL;
  if (hover_flags != ImGuizmoAxisFlags_ALL) {
    for (int axis_idx = 0; axis_idx < 3; ++axis_idx) {
      if (IsRotationAxisHovered(axis_idx)) {
        hover_flags |= AxisToFlag(axis_idx, false);
        break;
      }
    }
  }
  return hover_flags;
}
static glm::vec4 BuildRotationPlane() {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };

  glm::vec3 point, plane_normal;
  if (HasSingleAxis(hover_flags)) {
    point = gizmo->Mode == ImGuizmoMode_Local ? gizmo->ModelMatrix[3]
                                              : gizmo->SourceModelMatrix[3];
    plane_normal = gizmo->ModelMatrix[GetAxisIdx(hover_flags, false)];
  } else {
    point = gizmo->SourceModelMatrix[3];
    plane_normal = -g.Camera.Forward;
  }
  return BuildPlane(point, plane_normal);
}
static void BeginRotation() {
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  g.BackupModelMatrix = gizmo->SourceModelMatrix;
  g.DragOrigin = ImGui::GetIO().MousePos;

  gizmo->TranslationPlane = BuildRotationPlane();
  const float length{ IntersectRayPlane(g.Ray, gizmo->TranslationPlane) };
  gizmo->RotationVectorSource = glm::normalize(
    g.Ray.Origin + g.Ray.Direction * length - gizmo->ModelMatrix[3].xyz);
  gizmo->RotationAngleOrigin = gizmo->CalculateAngleOnPlane();
}
static void ContinueRotation(const float *snap) {
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  gizmo->RotationAngle = gizmo->CalculateAngleOnPlane();
  if (snap) CalculateSnap(gizmo->RotationAngle, glm::radians(snap[0]));

  glm::vec3 rotation_axis_local_space{ glm::normalize(
    gizmo->InversedModelMatrix *
    glm::vec4{ gizmo->TranslationPlane.xyz, 0.0f }) };

  const float angle{ gizmo->RotationAngle - gizmo->RotationAngleOrigin };
  const glm::mat4 delta_rotation{ glm::rotate(angle,
                                              rotation_axis_local_space) };

  if (gizmo->RotationAngle != gizmo->RotationAngleOrigin) {
    // @todo Handle locked axes ...

    if (gizmo->Mode == ImGuizmoMode_Local) {
      const glm::mat4 scale_origin{ glm::scale(gizmo->ModelScaleOrigin) };
      gizmo->ModelMatrix *= delta_rotation * scale_origin;
    } else {
      glm::mat4 result{ gizmo->SourceModelMatrix };
      result[3] = glm::vec4{ glm::vec3{ 0.0f }, 1.0f };
      gizmo->ModelMatrix = delta_rotation * result;
      gizmo->ModelMatrix[3] = gizmo->SourceModelMatrix[3];
    }
    gizmo->Dirty = true;
  }
  gizmo->RotationAngleOrigin = gizmo->RotationAngle;
}

//-----------------------------------------------------------------------------
// [SECTION] SCALE
//-----------------------------------------------------------------------------

static ImGuizmoAxisFlags FindScaleHover() {
  if (!CanActivate()) return ImGuizmoAxisFlags_None;

  ImGuizmoAxisFlags hover_flags{ ImGuizmoAxisFlags_None };
  if (IsCoreHovered()) hover_flags |= ImGuizmoAxisFlags_ALL;
  if (hover_flags != ImGuizmoAxisFlags_ALL) {
    for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
      if (IsAxisHovered(axis_idx)) {
        hover_flags |= AxisToFlag(axis_idx, false);
        break;
      }
  }
  return hover_flags;
}
static glm::vec4 BuildScalePlane() {
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  if (HasSingleAxis(hover_flags)) {
    const ImGuizmoAxis axis_idx{ GetAxisIdx(hover_flags, false) };
    return BuildPlane(gizmo->ModelMatrix[3],
                      gizmo->ModelMatrix[axis_idx == 2 ? 0 : axis_idx + 1]);
  } else {
    return BuildPlane(gizmo->ModelMatrix[3], gizmo->ModelMatrix[2]);
  }
}
static void BeginScale() {
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  g.BackupModelMatrix = gizmo->SourceModelMatrix;
  g.DragOrigin = ImGui::GetIO().MousePos;

  gizmo->Scale = glm::vec3{ 1.0f };
  gizmo->DragTranslationOrigin = gizmo->ModelMatrix[3];
  gizmo->TranslationPlane = BuildScalePlane();
  const float length{ IntersectRayPlane(g.Ray, gizmo->TranslationPlane) };
  gizmo->TranslationPlaneOrigin = g.Ray.Origin + g.Ray.Direction * length;
  gizmo->ModelRelativeOrigin =
    (gizmo->TranslationPlaneOrigin - gizmo->ModelMatrix[3].xyz()) *
    (1.0f / gizmo->ScreenFactor);

  for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
    gizmo->ScaleValueOrigin[axis_idx] =
      glm::length(gizmo->SourceModelMatrix[axis_idx]);
}
static void ContinueScale(const float *snap) {
  const ImGuiIO &io{ ImGui::GetIO() };
  const ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const float length{ IntersectRayPlane(g.Ray, gizmo->TranslationPlane) };
  const glm::vec3 target_position{ g.Ray.Origin + g.Ray.Direction * length };
  const glm::vec3 new_position{ target_position - gizmo->ModelRelativeOrigin *
                                                    gizmo->ScreenFactor };
  glm::vec3 delta{ new_position - gizmo->ModelMatrix[3].xyz };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  if (HasSingleAxis(hover_flags)) {
    const ImGuizmoAxis axis_idx{ GetAxisIdx(hover_flags, false) };
    const glm::vec3 axis_dir{ gizmo->ModelMatrix[axis_idx] };
    const float length_on_axis{ glm::dot(axis_dir, delta) };
    delta = axis_dir * length_on_axis;
    const glm::vec3 base_vec{ gizmo->TranslationPlaneOrigin -
                              gizmo->ModelMatrix[3].xyz };
    const float ratio{ glm::dot(axis_dir, base_vec + delta) /
                       glm::dot(axis_dir, base_vec) };
    // @todo Support for scale in World mode
    gizmo->Scale[axis_idx] = glm::max(ratio, 0.001f);
  } else {
    const float scale_delta{ (io.MousePos.x - g.DragOrigin.x) * 0.01f };
    gizmo->Scale = glm::vec3{ glm::max(1.0f + scale_delta, 0.001f) };
  }

  if (snap) {
    const float scale_snap[]{ snap[0], snap[0], snap[0] };
    CalculateSnap(gizmo->Scale, scale_snap);
  }

  for (int axis_idx = 0; axis_idx < 3; ++axis_idx) {
    gizmo->Scale[axis_idx] = glm::max(gizmo->Scale[axis_idx], 0.001f);
    if (gizmo->LockedAxesFlags & AxisToFlag(axis_idx, false))
      gizmo->Scale[axis_idx] = 1.0f;
  }

  if (gizmo->LastScale != gizmo->Scale) {
    gizmo->ModelMatrix *= glm::scale(gizmo->Scale * gizmo->ScaleValueOrigin);
    gizmo->Dirty = true;
  }
  gizmo->LastScale = gizmo->Scale;
}

//-----------------------------------------------------------------------------
// [SECTION] BOUNDS SCALE
//-----------------------------------------------------------------------------

ImGuizmoAxisFlags FindHoveredBound(const glm::mat4 &model_view_proj,
                                   ImGuizmoPlane &hovered_plane_idx,
                                   int &hovered_bound_idx) {
  const ImGuiIO &io{ ImGui::GetIO() };
  const ImGuizmoContext &g{ GImGuizmo };

  if (!CanActivate()) return ImGuizmoAxisFlags_None;

  ImGuizmoAxisFlags hover_flags{ ImGuizmoAxisFlags_None };
  for (int i = 0; i < 3; ++i) {
    const ImGuizmoPlane plane_idx{ g.MostVisiblePlanes[i] };
    if (!IsPlaneVisible(plane_idx)) continue;

    for (int i = 0; i < 4; ++i) {
      const glm::vec2 outter_bound{ WorldToScreen(
        g.Bounds.OutterPoints[plane_idx][i], model_view_proj) };
      const ImGuizmoAxis dir1_idx{ (plane_idx + 1) % 3 };
      const ImGuizmoAxis dir2_idx{ (plane_idx + 2) % 3 };
      if (glm::distance(outter_bound, glm::vec2{ io.MousePos }) <=
          kOutterAnchorSize) {
        hovered_bound_idx = i;
        hovered_plane_idx = plane_idx;
        hover_flags = PlaneToFlags(plane_idx);
        break;
      }
      const glm::vec2 mid_bound{ WorldToScreen(g.Bounds.MidPoints[plane_idx][i],
                                               model_view_proj) };
      if (glm::distance(mid_bound, glm::vec2{ io.MousePos }) <=
          kMidAnchorSize) {
        hovered_bound_idx = i;
        hovered_plane_idx = plane_idx;
        hover_flags = AxisToFlag((i + 2) % 2 ? dir2_idx : dir1_idx, false);
        break;
      }
    }
    if (hovered_bound_idx != -1) break;
  }
  return hover_flags;
}
void BuildOutterPoints(const float *bounds) {
  ImGuizmoContext &g{ GImGuizmo };

  for (int plane_idx = 0; plane_idx < 3; ++plane_idx) {
    const ImGuizmoAxis dir1_idx{ (plane_idx + 1) % 3 };
    const ImGuizmoAxis dir2_idx{ (plane_idx + 2) % 3 };
    const ImGuizmoAxis null_idx{ (plane_idx + 3) % 3 };
    for (int i = 0; i < 4; ++i) {
      g.Bounds.OutterPoints[plane_idx][i][null_idx] = 0.0f;
      g.Bounds.OutterPoints[plane_idx][i][dir1_idx] =
        bounds[dir1_idx + 3 * (i >> 1)];
      g.Bounds.OutterPoints[plane_idx][i][dir2_idx] =
        bounds[dir2_idx + 3 * ((i >> 1) ^ (i & 1))];
    }
  }
}
void BuildMidPoints() {
  ImGuizmoContext &g{ GImGuizmo };

  for (int plane_idx = 0; plane_idx < 3; ++plane_idx) {
    for (int i = 0; i < 4; ++i) {
      g.Bounds.MidPoints[plane_idx][i] =
        (g.Bounds.OutterPoints[plane_idx][i] +
         g.Bounds.OutterPoints[plane_idx][(i + 1) % 4]) *
        0.5f;
    }
  }
}
void BeginBoundsScale() {
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  g.BackupModelMatrix = gizmo->SourceModelMatrix;
  g.DragOrigin = ImGui::GetIO().MousePos;

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  ImGuizmoPlane hovered_plane_idx{ g.Bounds.ActivePlane };
  int hovered_bound_idx{ g.Bounds.ActiveBoundIdx };

  int opposite_index{ (hovered_bound_idx + 2) % 4 };
  if (HasPlane(hover_flags)) { // outter bound
    g.Bounds.Anchor = gizmo->SourceModelMatrix * glm::vec4{
      g.Bounds.OutterPoints[hovered_plane_idx][hovered_bound_idx], 1.0f
    };
    g.Bounds.LocalPivot =
      g.Bounds.OutterPoints[hovered_plane_idx][opposite_index];
  } else { // mid bound
    g.Bounds.Anchor = gizmo->SourceModelMatrix * glm::vec4{
      g.Bounds.MidPoints[hovered_plane_idx][hovered_bound_idx], 1.0f
    };
    g.Bounds.LocalPivot = g.Bounds.MidPoints[hovered_plane_idx][opposite_index];
  }
  g.Bounds.Pivot =
    gizmo->SourceModelMatrix * glm::vec4{ g.Bounds.LocalPivot, 1.0f };

  const glm::vec3 plane_normal{ glm::normalize(
    gizmo->SourceModelMatrix *
    glm::vec4{ kUnitDirections[hovered_plane_idx], 0.0f }) };
  gizmo->TranslationPlane = BuildPlane(g.Bounds.Anchor, plane_normal);
}
glm::vec3 ContinueBoundsScale(const float *bounds, const float *snap) {
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  const glm::vec3 reference_vector{ glm::abs(g.Bounds.Anchor -
                                             g.Bounds.Pivot) };
  const float length{ IntersectRayPlane(g.Ray, gizmo->TranslationPlane) };
  const glm::vec3 target_position{ g.Ray.Origin + g.Ray.Direction * length };
  const glm::vec3 delta{ glm::abs(target_position - g.Bounds.Pivot) };

  const ImGuizmoAxisFlags hover_flags{ gizmo->ActiveManipulationFlags };
  ImGuizmoAxis axes[2]{ -1, -1 };
  if (HasPlane(hover_flags)) {
    axes[0] = (g.Bounds.ActivePlane + 1) % 3;
    axes[1] = (g.Bounds.ActivePlane + 2) % 3;
  } else
    axes[0] = GetAxisIdx(hover_flags, false);

  glm::mat4 scale{ 1.0f };
  for (int i = 0; i < 2; ++i) {
    const ImGuizmoAxis axis_idx{ axes[i] };
    if (axis_idx == -1) continue;

    const glm::vec3 axis_dir{ glm::abs(g.BackupModelMatrix[axis_idx]) };
    const float dt_axis{ glm::dot(axis_dir, reference_vector) };

    float ratio_axis{ 1.0f };
    if (dt_axis > kEpsilon) ratio_axis = glm::dot(axis_dir, delta) / dt_axis;

    if (snap) {
      const float bound_size{ bounds[axis_idx + 3] - bounds[axis_idx] };
      float length{ bound_size * ratio_axis };
      CalculateSnap(length, snap[axis_idx]);
      if (bound_size > kEpsilon) ratio_axis = length / bound_size;
    }
    scale[axis_idx] *= ratio_axis;
  }

  gizmo->ModelMatrix = g.BackupModelMatrix *
                       glm::translate(g.Bounds.LocalPivot) * scale *
                       glm::translate(-g.Bounds.LocalPivot);
  gizmo->Dirty = true;

  glm::vec3 scale_info{};
  for (int axis_idx = 0; axis_idx < 3; ++axis_idx) {
    scale_info[axis_idx] = (bounds[axis_idx + 3] - bounds[axis_idx]) *
                           glm::length(g.BackupModelMatrix[axis_idx]) *
                           glm::length(scale[axis_idx]);
  }
  return scale_info;
}

//-----------------------------------------------------------------------------
// [SECTION] PUBLIC INTERFACE
//-----------------------------------------------------------------------------

void PrintContext() {
  const ImGuiIO &io{ ImGui::GetIO() };
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ g.CurrentGizmo };

  const glm::vec2 top_left{ g.Viewport.GetTL() };
  const glm::vec2 size{ g.Viewport.GetSize() };
  ImGui::Text("Viewport = (%.f,%.f) %.fx%.f", top_left.x, top_left.y, size.x,
              size.y);
  ImGui::Text("DragOrigin = (%.f, %.f)", g.DragOrigin.x, g.DragOrigin.y);

  ImGui::SetNextItemOpen(true);
  if (ImGui::TreeNode("Camera")) {
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

    ImGui::InputFloat3("Right", glm::value_ptr(g.Camera.Right), "%.2f");
    ImGui::InputFloat3("Up", glm::value_ptr(g.Camera.Up), "%.2f");
    ImGui::InputFloat3("Forward", glm::value_ptr(g.Camera.Forward), "%.2f");
    ImGui::InputFloat3("Eye", glm::value_ptr(g.Camera.Eye), "%.2f");

    ImGui::PopItemFlag();
    ImGui::TreePop();
  }

  ImGui::SetNextItemOpen(true);
  if (ImGui::TreeNode("Ray")) {
    ImGui::Text("x: %.f y: %.f", io.MousePos.x, io.MousePos.y);

    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImGui::InputFloat3("Start", glm::value_ptr(g.Ray.Origin), "%.2f");
    ImGui::InputFloat3("End", glm::value_ptr(g.Ray.End), "%.2f");
    ImGui::InputFloat3("Direction", glm::value_ptr(g.Ray.Direction), "%.2f");

    ImGui::PopItemFlag();
    ImGui::TreePop();
  }

  if (gizmo) {
    ImGui::Text("ID = %d", gizmo->ID);
    ImGui::Text("ActiveOperation: %d", gizmo->ActiveOperation);
    ImGui::Text("ActiveManipulationFlags: %s",
                StatAxisFlags(gizmo->ActiveManipulationFlags));

    ImGui::SetNextItemOpen(true);
    if (ImGui::TreeNode("Gizmo")) {
      ImGui::Text("Origin: [%.2f, %.2f]", gizmo->Origin.x, gizmo->Origin.y);
      ImGui::Text("RingRadius: %.2f", gizmo->RingRadius);
      ImGui::Text("ScreenFactor: %.2f", gizmo->ScreenFactor);
      ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true);
    if (ImGui::TreeNode("Shared")) {
      ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

      ImGui::InputFloat4("TranslationPlane",
                         glm::value_ptr(gizmo->TranslationPlane));
      ImGui::InputFloat3("TranslationPlaneOrigin",
                         glm::value_ptr(gizmo->TranslationPlaneOrigin));
      ImGui::InputFloat3("ModelRelativeOrigin",
                         glm::value_ptr(gizmo->ModelRelativeOrigin));
      ImGui::InputFloat3("DragTranslationOrigin",
                         glm::value_ptr(gizmo->DragTranslationOrigin));

      ImGui::PopItemFlag();
      ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true);
    if (ImGui::TreeNode("Translation")) {
      ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

      ImGui::InputFloat3("LastTranslationDelta",
                         glm::value_ptr(gizmo->LastTranslationDelta));

      ImGui::PopItemFlag();
      ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true);
    if (ImGui::TreeNode("Rotation")) {
      ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

      ImGui::InputFloat3("ModelScaleOrigin",
                         glm::value_ptr(gizmo->ModelScaleOrigin));
      ImGui::InputFloat3("RotationVectorSource",
                         glm::value_ptr(gizmo->RotationVectorSource));

      ImGui::Text("RotationAngle: %.2f rad", gizmo->RotationAngle);
      ImGui::Text("RotationAngleOrigin: %.2f rad", gizmo->RotationAngleOrigin);

      ImGui::PopItemFlag();
      ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true);
    if (ImGui::TreeNode("Scale")) {
      ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

      ImGui::InputFloat3("Scale", glm::value_ptr(gizmo->Scale));
      ImGui::InputFloat3("LastScale", glm::value_ptr(gizmo->LastScale));
      ImGui::InputFloat3("ScaleValueOrigin",
                         glm::value_ptr(gizmo->ScaleValueOrigin));

      ImGui::PopItemFlag();
      ImGui::TreePop();
    }
  }

  ImGui::SetNextItemOpen(true);
  if (ImGui::TreeNode("Bounds")) {
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

    ImGui::InputFloat3("Anchor", &g.Bounds.Anchor[0]);
    ImGui::InputFloat3("LocalPivot", &g.Bounds.LocalPivot[0]);
    ImGui::InputFloat3("Pivot", &g.Bounds.Pivot[0]);
    ImGui::Text("ActivePlane = %s", g.Bounds.ActivePlane == -1
                                      ? "None"
                                      : GetPlaneName(g.Bounds.ActivePlane));
    ImGui::Text("ActiveBoundIdx = %d", g.Bounds.ActiveBoundIdx);

    ImGui::PopItemFlag();
    ImGui::TreePop();
  }
}

void SetConfigFlags(ImGuizmoConfigFlags flags) {
  GImGuizmo.ConfigFlags = flags;
}

void SetViewport(const ImVec2 &position, const ImVec2 &size) {
  GImGuizmo.Viewport = ImRect{ position, position + size };
}
void SetViewport(float x, float y, float width, float height) {
  SetViewport({ x, y }, { width, height });
}
void SetDrawlist(ImDrawList *drawList) {
  GImGuizmo.DrawList = drawList ? drawList : ImGui::GetWindowDrawList();
}

void SetCamera(const float *view_matrix, const float *projection_matrix,
               bool is_ortho) {
  IM_ASSERT(view_matrix && projection_matrix);

  ImGuizmoContext &g{ GImGuizmo };

  g.Camera.ViewMatrix = glm::make_mat4(view_matrix);
  const glm::mat4 inversed_view_matrix{ glm::inverse(g.Camera.ViewMatrix) };
  g.Camera.Right = inversed_view_matrix[0];
  g.Camera.Up = inversed_view_matrix[1];
  g.Camera.Forward = inversed_view_matrix[2];
  g.Camera.Eye = inversed_view_matrix[3];

  g.Camera.IsOrtho = is_ortho;
  g.Camera.ProjectionMatrix = glm::make_mat4(projection_matrix);

  g.Camera.ViewProjectionMatrix =
    g.Camera.ProjectionMatrix * g.Camera.ViewMatrix;
}

bool Manipulate(ImGuizmoMode mode, ImGuizmoOperation operation,
                float *model_matrix, const float *snap) {
  if (Begin(mode, model_matrix)) {
    switch (operation) {
    case ImGuizmoOperation_Translate:
      Translate(snap);
      break;
    case ImGuizmoOperation_Rotate:
      Rotate(snap);
      break;
    case ImGuizmoOperation_Scale:
      Scale(snap);
      break;
    }
  }
  return End();
}

bool Begin(ImGuizmoMode mode, float *model_matrix,
           ImGuizmoAxisFlags locked_axes) {
  ImGuizmoContext &g{ GImGuizmo };

  IM_ASSERT(model_matrix && "Model matrix required");
  IM_ASSERT(!g.LockedModelMatrix && "Nesting forbidden");

  g.LockedModelMatrix = model_matrix;
  if (!g.DrawList) g.DrawList = ImGui::GetWindowDrawList();
  g.Viewport = CalculateViewport();

  auto id = static_cast<ImGuiID>(reinterpret_cast<long long>(model_matrix));
  ImGuizmoWidget *gizmo{ FindGizmoById(id) };
  if (!gizmo) gizmo = CreateNewGizmo(id);
  g.CurrentGizmo = gizmo;

  if (!ImGui::GetIO().MouseDown[0]) {
    g.DragOrigin = glm::vec2{ 0.0f };
    g.ActiveGizmo = nullptr;
    gizmo->ActiveManipulationFlags = ImGuizmoAxisFlags_None;
  }
  if (!gizmo->ActiveManipulationFlags) {
    gizmo->ActiveOperation = ImGuizmoOperation_None;
    g.Bounds.ActivePlane = -1;
    g.Bounds.ActiveBoundIdx = -1;
  }

  gizmo->Mode = mode;
  gizmo->Load(model_matrix);
  gizmo->LockedAxesFlags = locked_axes;
  g.Ray = RayCast(g.Camera.ViewProjectionMatrix);

  for (int plane_idx = 0; plane_idx < 3; ++plane_idx) {
    const glm::vec3 plane_normal{ glm::normalize(
      gizmo->SourceModelMatrix *
      glm::vec4{ kUnitDirections[plane_idx], 0.0f }) };
    g.PlanesVisibility[plane_idx] = glm::abs(
      glm::dot(glm::normalize(g.Camera.Eye - gizmo->SourceModelMatrix[3].xyz),
               plane_normal));
  }
  ImQsort(g.MostVisiblePlanes, 3, sizeof(ImGuizmoPlane),
          [](const void *a, const void *b) {
            const ImGuizmoContext &g{ GImGuizmo };
            float fa{ g.PlanesVisibility[*static_cast<const int *>(a)] };
            float fb{ g.PlanesVisibility[*static_cast<const int *>(b)] };
            return (fa > fb) ? -1 : (fa < fb);
          });

  const glm::vec3 camera_space_position{ gizmo->ModelViewProjMatrix *
                                         glm::vec4{ glm::vec3{ 0.0f }, 1.0f } };
  return g.Camera.IsOrtho ? true : camera_space_position.z >= 0.001f;
}
bool End() {
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  IM_ASSERT(g.LockedModelMatrix && "It seems that you didn't call Begin()");

  if (g.ConfigFlags & ImGuizmoConfigFlags_HasReversing &&
      ImGui::GetIO().MouseClicked[1] && gizmo->ActiveManipulationFlags) {
    gizmo->ModelMatrix = g.BackupModelMatrix;
    gizmo->Dirty = true;
    gizmo->ActiveManipulationFlags = ImGuizmoAxisFlags_None;
  }

  bool updated{ false };
  if (gizmo->Dirty) {
    *reinterpret_cast<glm::mat4 *>(g.LockedModelMatrix) = gizmo->ModelMatrix;
    gizmo->Dirty = false;
    updated = true;
  }
  g.LockedModelMatrix = nullptr;
  // g.CurrentGizmo = nullptr;

  return updated;
}

void Translate(const float *snap) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  ImGuizmoAxisFlags hover_flags{ FindTranslationHover() };
  bool held;
  if (GizmoBehavior(ImGuizmoOperation_Translate, hover_flags, &held))
    BeginTranslation();
  if (held) ContinueTranslation(snap);

  if (gizmo->ActiveOperation == ImGuizmoOperation_Translate)
    RenderTranslationTrail();

  if (!gizmo->ActiveManipulationFlags ||
      !(g.ConfigFlags & ImGuizmoConfigFlags_CloakOnManipulate)) {
    for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
      RenderTranslateAxis(axis_idx, hover_flags);
    for (int plane_idx = 0; plane_idx < 3; ++plane_idx)
      RenderPlane(plane_idx, hover_flags);
    RenderCore(hover_flags);
  }

  if (gizmo->ActiveOperation == ImGuizmoOperation_Translate)
    RenderTranslationInfo();
}
void Rotate(const float *snap) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  ImGuizmoAxisFlags hover_flags{ FindRotationHover() };
  bool held;
  if (GizmoBehavior(ImGuizmoOperation_Rotate, hover_flags, &held))
    BeginRotation();
  if (held) ContinueRotation(snap);

  if (gizmo->ActiveManipulationFlags &&
      (g.ConfigFlags & ImGuizmoConfigFlags_CloakOnManipulate)) {
    if (HasSingleAxis(hover_flags))
      RenderRotationAxis(GetAxisIdx(hover_flags, true), true, hover_flags);
    else if (hover_flags == ImGuizmoAxisFlags_ALL) {
      RenderRotationRing(hover_flags);
    }
  } else {
    for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
      RenderRotationAxis(axis_idx, false, hover_flags);
    RenderRotationRing(hover_flags);
  }

  if (gizmo->ActiveOperation == ImGuizmoOperation_Rotate) {
    RenderRotationTrail();
    RenderRotationInfo();
  }
}
void Scale(const float *snap) {
  const ImGuizmoContext &g{ GImGuizmo };
  const ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  ImGuizmoAxisFlags hover_flags{ FindScaleHover() };
  bool held;
  if (GizmoBehavior(ImGuizmoOperation_Scale, hover_flags, &held)) {
    BeginScale();
  }
  if (held) ContinueScale(snap);

  if (!gizmo->ActiveManipulationFlags ||
      !(g.ConfigFlags & ImGuizmoConfigFlags_CloakOnManipulate)) {
    for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
      RenderScaleAxis(axis_idx, hover_flags);
#if 0
    // @todo 
    for (int plane_idx = 0; plane_idx < 3; ++plane_idx)
      RenderPlane(plane_idx, hover_flags);
#endif
    RenderCore(hover_flags);
  }

  if (gizmo->ActiveOperation == ImGuizmoOperation_Scale) {
    if (hover_flags == ImGuizmoAxisFlags_ALL) {
      for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
        RenderScaleTrail(axis_idx);
    } else
      RenderScaleTrail(GetAxisIdx(hover_flags, false));
    RenderScaleInfo(gizmo->Scale);
  }
}
void BoundsScale(const float *bounds, const float *snap) {
  ImGuizmoContext &g{ GImGuizmo };
  ImGuizmoWidget *gizmo{ GetCurrentGizmo() };

  BuildOutterPoints(bounds);
  BuildMidPoints();

  const glm::mat4 model_view_proj{ g.Camera.ViewProjectionMatrix *
                                   gizmo->SourceModelMatrix };

  ImGuizmoPlane hovered_plane_idx{ -1 };
  int hovered_bound_idx{ -1 };
  ImGuizmoAxisFlags hover_flags{ FindHoveredBound(
    model_view_proj, hovered_plane_idx, hovered_bound_idx) };
  IM_ASSERT(hovered_plane_idx < 3 && hovered_bound_idx < 4);

  bool held;
  if (BoundBehavior(hover_flags, hovered_plane_idx, hovered_bound_idx, &held))
    BeginBoundsScale();

  glm::vec3 scale_info{};
  if (held) scale_info = ContinueBoundsScale(bounds, snap);

  if (!gizmo->ActiveManipulationFlags ||
      gizmo->ActiveOperation == ImGuizmoOperation_BoundsScale) {
    RenderBounds(model_view_proj, hover_flags, hovered_plane_idx,
                 hovered_bound_idx);
  }

  if (gizmo->ActiveOperation == ImGuizmoOperation_BoundsScale)
    RenderScaleInfo(scale_info);
}

void ViewManipulate(float *view_matrix, const float length,
                    const ImVec2 &position, const ImVec2 &size,
                    ImU32 background_color) {
  const ImGuiIO &io{ ImGui::GetIO() };
  ImDrawList *draw_list{ ImGui::GetWindowDrawList() };

  const ImRect bb{ position, position + size };
  draw_list->AddRectFilled(bb.Min, bb.Max, background_color);

  // ---

  const glm::mat4 inversed_view_matrix{ glm::inverse(
    glm::make_mat4(view_matrix)) };
  const glm::vec3 forward{ inversed_view_matrix[2][0],
                           inversed_view_matrix[2][1],
                           inversed_view_matrix[2][2] };
  const glm::vec3 up{ inversed_view_matrix[1][0], inversed_view_matrix[1][1],
                      inversed_view_matrix[1][2] };

  constexpr float distance{ 2.0f };
  const glm::mat4 manip_view{ glm::lookAt(forward * distance, glm::vec3{ 0.0f },
                                          up) };
  const glm::mat4 manip_projection{ glm::perspective(
    glm::radians(60.0f), bb.GetWidth() / bb.GetHeight(), 0.1f, 10.0f) };

  const glm::mat4 manip_view_proj{ manip_projection * manip_view };
  const ImGuizmoRay ray{ RayCast(manip_view_proj, bb) };

  // ---

  bool hovered{ ImGui::IsWindowHovered() && bb.Contains(io.MousePos) };
  bool held;
  bool pressed{ ViewManipulatorBehavior(hovered, &held) };

  static bool animate{ false };
  static glm::vec3 target_up{ 0.0f };
  static glm::vec3 target_forward{ 0.0f };
  if (held) animate = false;

  static const glm::vec2 panel_positions[]{ { 0.75f, 0.75f }, { 0.25f, 0.75f },
                                            { 0.00f, 0.75f }, { 0.75f, 0.25f },
                                            { 0.25f, 0.25f }, { 0.00f, 0.25f },
                                            { 0.75f, 0.00f }, { 0.25f, 0.00f },
                                            { 0.00f, 0.00f } };
  static const glm::vec2 panel_sizes[]{ { 0.25f, 0.25f }, { 0.50f, 0.25f },
                                        { 0.25f, 0.25f }, { 0.25f, 0.50f },
                                        { 0.50f, 0.50f }, { 0.25f, 0.50f },
                                        { 0.25f, 0.25f }, { 0.50f, 0.25f },
                                        { 0.25f, 0.25f } };

  bool cubes[27]{};
  for (int pass = 0; pass < 2; ++pass) {
    for (int face = 0; face < 6; ++face) {
      const int normal_idx{ face % 3 };
      const int perp_x_idx{ (normal_idx + 1) % 3 };
      const int perp_y_idx{ (normal_idx + 2) % 3 };

      const float invert{ (face > 2) ? -1.0f : 1.0f };
      const glm::vec3 index_vector_x{ kUnitDirections[perp_x_idx] * invert };
      const glm::vec3 index_vector_y{ kUnitDirections[perp_y_idx] * invert };
      const glm::vec3 box_origin{ kUnitDirections[normal_idx] * -invert -
                                  index_vector_x - index_vector_y };

      const glm::vec3 n{ kUnitDirections[normal_idx] * invert };
      const glm::vec3 view_space_normal{ glm::normalize(manip_view *
                                                        glm::vec4{ n, 0.0f }) };
      const glm::vec3 view_space_point{ manip_view *
                                        glm::vec4{ n * 0.5f, 1.0f } };
      const glm::vec4 view_space_face_plane{ BuildPlane(view_space_point,
                                                        view_space_normal) };

      if (view_space_face_plane.w > 0.0f) continue; // Back face culling

      const glm::vec4 face_plane{ BuildPlane(n * 0.5f, n) };
      const float lenght{ IntersectRayPlane(ray, face_plane) };
      const glm::vec3 pos_on_plane{ ray.Origin + ray.Direction * lenght -
                                    (n * 0.5f) };
      const float local_x{
        glm::dot(kUnitDirections[perp_x_idx], pos_on_plane) * invert + 0.5f
      };
      const float local_y{
        glm::dot(kUnitDirections[perp_y_idx], pos_on_plane) * invert + 0.5f
      };

      const glm::vec3 dx{ kUnitDirections[perp_x_idx] };
      const glm::vec3 dy{ kUnitDirections[perp_y_idx] };
      const glm::vec3 origin{ kUnitDirections[normal_idx] - dx - dy };
      for (int panel = 0; panel < 9; ++panel) {
        const glm::vec2 p{ panel_positions[panel] * 2.0f };
        const glm::vec2 s{ panel_sizes[panel] * 2.0f };
        const glm::vec3 panel_pos[4]{ dx * p.x + dy * p.y,
                                      dx * p.x + dy * (p.y + s.y),
                                      dx * (p.x + s.x) + dy * (p.y + s.y),
                                      dx * (p.x + s.x) + dy * p.y };

        ImVec2 face_coords_screen[4];
        for (auto coord = 0; coord < 4; ++coord) {
          face_coords_screen[coord] = WorldToScreen(
            (panel_pos[coord] + origin) * 0.5f * invert, manip_view_proj, bb);
        }

        const glm::vec2 panel_corners[2]{
          panel_positions[panel], panel_positions[panel] + panel_sizes[panel]
        };
        const bool panel_hovered{ local_x > panel_corners[0].x &&
                                  local_x < panel_corners[1].x &&
                                  local_y > panel_corners[0].y &&
                                  local_y < panel_corners[1].y };

        const glm::vec3 box_coord{
          box_origin + index_vector_x * static_cast<float>(panel % 3) +
          index_vector_y * static_cast<float>(panel / 3) + glm::vec3{ 1.0f }
        };
        const auto cube_idx{ static_cast<int>(
          box_coord.x * 9.0f + box_coord.y * 3.0f + box_coord.z) };
        IM_ASSERT(cube_idx < 27);
        cubes[cube_idx] |= panel_hovered && !held;
        if (pass) {
          draw_list->AddConvexPolyFilled(
            face_coords_screen, 4,
            (GetColorU32(ImGuizmoCol_AxisX + normal_idx) | 0xFF1F1F1F) |
              (hovered ? 0x080808 : 0));
          if (cubes[cube_idx]) {
            draw_list->AddConvexPolyFilled(
              face_coords_screen, 4, GetColorU32(ImGuizmoCol_Hovered, 0.541f));
            if (pressed) {
              const int cx{ cube_idx / 9 };
              const int cy{ (cube_idx - cx * 9) / 3 };
              const int cz{ cube_idx % 3 };
              target_forward = glm::normalize(1.0f - glm::vec3{ cx, cy, cz });
              if (glm::abs(glm::dot(target_forward, kReferenceUp)) >
                  1.0f - 0.01f) {
                glm::vec3 right{ inversed_view_matrix[0] };
                if (glm::abs(right.x) > glm::abs(right.z))
                  right.z = 0.0f;
                else
                  right.x = 0.0f;
                right = glm::normalize(right);
                target_up = glm::normalize(glm::cross(target_forward, right));
              } else {
                target_up = kReferenceUp;
              }
              animate = true;
              pressed = false;
            }
          }
        }
      }
    }
  }

  const glm::vec3 target_pos{ inversed_view_matrix[3] -
                              inversed_view_matrix[2] * length };

  if (animate) {
    constexpr float speed{ 10.0f };
    const float a{ speed * io.DeltaTime };
    const glm::vec3 interpolated_forward{ glm::normalize(
      glm::lerp(inversed_view_matrix[2].xyz(), target_forward, a)) };

    if (glm::distance(interpolated_forward, target_forward) < 0.001f)
      animate = false;

#if 0
    const glm::vec3 interpolated_up{ glm::normalize(
      glm::lerp(inversed_view_matrix[1].xyz(), target_up, a)) };
    ImGui::Text("dot = %.2f", glm::dot(interpolated_up, target_up));
#endif

    const glm::vec3 new_eye{ target_pos + interpolated_forward * length };
    *reinterpret_cast<glm::mat4 *>(view_matrix) =
      glm::lookAt(new_eye, target_pos, target_up);
  }

  const glm::vec2 mouse_delta{ io.MouseDelta };
  if (held && glm::any(glm::notEqual(mouse_delta, glm::vec2{ 0.0f }))) {
    constexpr float drag_sensitivity{ 0.01f };
    const glm::vec2 angles{ -mouse_delta * drag_sensitivity };
    const glm::mat4 rx{ glm::rotate(angles.x, kReferenceUp) };
    const glm::mat4 ry{ glm::rotate(angles.y, inversed_view_matrix[0].xyz()) };
    const glm::mat4 roll{ ry * rx };
    const glm::vec3 new_forward{ glm::normalize(roll *
                                                inversed_view_matrix[2]) };
    glm::vec3 plane_dir{ glm::cross(inversed_view_matrix[0].xyz(),
                                    kReferenceUp) };
    plane_dir.y = 0.0f;
    plane_dir = glm::normalize(plane_dir);
    if (glm::dot(plane_dir, new_forward) > 0.05f) {
      const glm::vec3 new_eye{ target_pos + new_forward * length };
      *reinterpret_cast<glm::mat4 *>(view_matrix) =
        glm::lookAt(new_eye, target_pos, kReferenceUp);
    }
  }
}

void ViewManipulate(float *view_matrix, const float length,
                    ImU32 background_color) {
  const ImRect bb{ CalculateViewport() };
  ViewManipulate(view_matrix, length, bb.GetTL(), bb.GetSize(),
                 background_color);
}

void DecomposeMatrix(const float *matrix, float *translation, float *rotation,
                     float *scale) {
  IM_ASSERT(matrix);

  auto mat = *reinterpret_cast<const glm::mat4 *>(matrix);
  for (int axis_idx = 0; axis_idx < 3; ++axis_idx) {
    if (scale) scale[axis_idx] = glm::length(mat[axis_idx]);
    mat[axis_idx] = glm::normalize(mat[axis_idx]);
  }
  if (rotation) {
    rotation[0] = glm::degrees(glm::atan(mat[1][2], mat[2][2]));
    rotation[1] = glm::degrees(glm::atan(
      -mat[0][2], glm::sqrt(mat[1][2] * mat[1][2] + mat[2][2] * mat[2][2])));
    rotation[2] = glm::degrees(glm::atan(mat[0][1], mat[0][0]));
  }
  if (translation) memcpy(translation, &mat[3], sizeof(float) * 3);
}
void RecomposeMatrix(const float *translation, const float *rotation,
                     const float *scale, float *matrix) {
  IM_ASSERT(matrix && translation && rotation && scale);

  glm::mat4 mat{ 1.0f };

  // Rotate
  for (int axis_idx = 2; axis_idx >= 0; --axis_idx) {
    mat *=
      glm::rotate(glm::radians(rotation[axis_idx]), kUnitDirections[axis_idx]);
  }
  // Scale
  for (int axis_idx = 0; axis_idx < 3; ++axis_idx) {
    const auto valid_scale =
      glm::abs(scale[axis_idx]) < kEpsilon ? 0.001f : scale[axis_idx];
    mat[axis_idx] *= valid_scale;
  }
  // Translate
  mat[3] = glm::vec4{ glm::make_vec3(translation), 1.0f };
  *reinterpret_cast<glm::mat4 *>(matrix) = std::move(mat);
}

}; // namespace ImGuizmo

using namespace ImGuizmo;

//-----------------------------------------------------------------------------
// [SECTION] ImGuizmoContext METHODS
//-----------------------------------------------------------------------------

ImGuizmoContext::~ImGuizmoContext() {
  for (int i = 0; i < Gizmos.Size; ++i)
    delete Gizmos[i];

  Gizmos.clear();
  GizmosById.Clear();
  CurrentGizmo = nullptr;
}
float ImGuizmoContext::GetAspectRatio() const {
  return Viewport.GetWidth() / Viewport.GetHeight();
}

//-----------------------------------------------------------------------------
// [SECTION] ImGuizmoWidget METHODS
//-----------------------------------------------------------------------------

void ImGuizmoWidget::Load(const float *model) {
  const ImGuizmoContext &g{ GImGuizmo };

  SourceModelMatrix = glm::make_mat4(model);
  if (Mode == ImGuizmoMode_Local) {
    ModelMatrix = SourceModelMatrix;
    for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
      ModelMatrix[axis_idx] = glm::normalize(ModelMatrix[axis_idx]);
  } else {
    ModelMatrix = glm::translate(SourceModelMatrix[3].xyz());
  }
  ModelViewProjMatrix = g.Camera.ViewProjectionMatrix * ModelMatrix;

  for (int axis_idx = 0; axis_idx < 3; ++axis_idx)
    ModelScaleOrigin[axis_idx] = glm::length(SourceModelMatrix[axis_idx]);

  InversedModelMatrix = glm::inverse(ModelMatrix);
  const glm::vec3 right_view_inverse{ InversedModelMatrix *
                                      glm::vec4{ g.Camera.Right, 0.0f } };
  const float right_length{ GetSegmentLengthClipSpace(glm::vec3{ 0.0f },
                                                      right_view_inverse) };
  ScreenFactor = g.Style.GizmoScale / right_length;
  Origin = WorldToScreen(glm::vec3{ 0.0f }, ModelViewProjMatrix);
  RingRadius = g.Style.GizmoScale /* 1.04f*/ * g.Viewport.GetWidth() * 0.55f;
  // RingRadius = 0.06 * g.Viewport.GetHeight();
}
float ImGuizmoWidget::CalculateAngleOnPlane() const {
  const ImGuizmoContext &g{ GImGuizmo };

  const float length{ IntersectRayPlane(g.Ray, TranslationPlane) };
  const glm::vec3 local_pos{ glm::normalize(
    g.Ray.Origin + g.Ray.Direction * length - ModelMatrix[3].xyz) };

  const glm::vec3 perpendicular_vec{ glm::normalize(
    glm::cross(RotationVectorSource, TranslationPlane.xyz())) };

  const float acos_angle{ glm::clamp(glm::dot(local_pos, RotationVectorSource),
                                     -1.0f, 1.0f) };
  float angle{ glm::acos(acos_angle) };
  angle *= (glm::dot(local_pos, perpendicular_vec) < 0.0f) ? 1.0f : -1.0f;
  return angle;
}