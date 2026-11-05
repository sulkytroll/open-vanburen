#pragma once
// Skeletal animation runtime on top of gr2.h: skeleton/animation lookup by model name, curve sampling and skinning matrices.
// Matrices are 4x4 row-major with the ROW-VECTOR convention (p' = p * M, translation in the last row), as Granny stores them.
#include "gr2.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vb {

// Skeleton/animation translations are in units 10x smaller than the G3D meshes' (a 6-unit-tall mesh has a 0.6-tall skeleton; measured against
// the mesh bone-weight centroids), so positions are multiplied by this when posing.
constexpr float kSkeletonUnitScale = 10.f;

class AnimLibrary {
public:
    explicit AnimLibrary(const std::string& assetsRoot);
    // The skeleton (.skel) a G3D model skins to: "HumMNor_BodVaultSuit" -> HumMNor, "CR_Cow" -> CR_Cow, "DS_..{2}MainDoor" -> same stem.
    // Returns nullptr when the model has no skeleton file.
    const GrSkeleton* skeletonFor(const std::string& g3dStem);
    // Stems of the animation files that belong to the model (HumM_*/HumF_* for humans, <stem>_* otherwise).
    std::vector<std::string> animationsFor(const std::string& g3dStem) const;
    // The default idle animation stem for a model ("HumM_Def_Una_Idle_01", "CR_Cow_Def_Una_Idle_01", ...) or "" if it has none.
    std::string idleFor(const std::string& g3dStem) const;
    const GrAnimation* animation(const std::string& animStem);     // first animation of the file; nullptr if unknown/invalid
private:
    std::string skelKey(const std::string& g3dStem) const;
    std::map<std::string, std::string> skel_, anim_;               // lowercase stem -> path
    std::map<std::string, std::unique_ptr<Gr2>> cache_;
    const Gr2* load(const std::string& path);
};

// Local-to-parent matrix from a transform (scale/shear, rotation quaternion, translation).
void transformMatrix(const GrTransform& t, float m[16]);

// Evaluate a curve at time t (keyframes interpolated linearly; quaternions normalised).
void sampleCurve(const GrCurve& c, float t, float* out);

// Skinning matrices (16 floats per skeleton bone: inverse bind world * animated world). `anim` may be nullptr for the bind pose.
// worldOut (optional): each bone's model-space matrix (column-major, 16 floats per bone) -- where things attached to a bone (a held weapon) go
void poseSkeleton(const GrSkeleton& sk, const GrAnimation* anim, float time, std::vector<float>& skin, std::vector<float>* worldOut = nullptr);

} // namespace vb
