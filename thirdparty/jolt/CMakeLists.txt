# Jolt Physics (jrouwe/JoltPhysics) — vendored, MIT (see LICENSE).
# Library-only subtree (Jolt/) pinned to upstream v5.6.0
# (commit e77f175595e64cb44218cc9d9d56fc365ad0e36a; see README.md).
# Samples / UnitTests / TestFramework / PerformanceTest / Docs / Assets /
# CI and editor-tooling extras are not vendored; this target compiles the
# library with GPU compute and debug-renderer support disabled (defaults).
#
# The source list mirrors upstream Jolt/Jolt.cmake (v5.6.0), trimmed to the
# .cpp translation units; DX12/VK/MTL/CPU-compute TUs are internally
# #ifdef-guarded and compile to no-ops without their JPH_USE_* defines.

set(JOLT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(jolt STATIC
    ${JOLT_SOURCE_DIR}/Jolt/AABBTree/AABBTreeBuilder.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/CPU/ComputeBufferCPU.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/CPU/ComputeQueueCPU.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/CPU/ComputeSystemCPU.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/ComputeSystem.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/DX12/ComputeBufferDX12.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/DX12/ComputeQueueDX12.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/DX12/ComputeSystemDX12.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/DX12/ComputeSystemDX12Impl.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/VK/ComputeBufferVK.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/VK/ComputeQueueVK.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/VK/ComputeShaderVK.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/VK/ComputeSystemVK.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/VK/ComputeSystemVKImpl.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Compute/VK/ComputeSystemVKWithAllocator.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/Color.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/Factory.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/IssueReporting.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/JobSystemSingleThreaded.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/JobSystemThreadPool.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/JobSystemWithBarrier.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/LinearCurve.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/Memory.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/Profiler.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/RTTI.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/Semaphore.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/StringTools.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Core/TickCounter.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Geometry/ConvexHullBuilder.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Geometry/ConvexHullBuilder2D.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Geometry/Indexify.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Geometry/OrientedBox.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Math/Vec3.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/ObjectStream.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/ObjectStreamBinaryIn.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/ObjectStreamBinaryOut.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/ObjectStreamIn.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/ObjectStreamOut.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/ObjectStreamTextIn.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/ObjectStreamTextOut.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/SerializableObject.cpp
    ${JOLT_SOURCE_DIR}/Jolt/ObjectStream/TypeDeclarations.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Body/Body.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Body/BodyCreationSettings.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Body/BodyInterface.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Body/BodyManager.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Body/MassProperties.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Body/MotionProperties.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Character/Character.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Character/CharacterBase.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Character/CharacterVirtual.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/BroadPhase/BroadPhase.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/BroadPhase/BroadPhaseBruteForce.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/BroadPhase/BroadPhaseQuadTree.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/BroadPhase/QuadTree.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/CastConvexVsTriangles.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/CastSphereVsTriangles.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/CollideConvexVsTriangles.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/CollideSphereVsTriangles.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/CollisionDispatch.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/CollisionGroup.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/EstimateCollisionResponse.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/GroupFilter.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/GroupFilterTable.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/ManifoldBetweenTwoFaces.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/NarrowPhaseQuery.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/NarrowPhaseStats.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/PhysicsMaterial.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/PhysicsMaterialSimple.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/BoxShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/CapsuleShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/CompoundShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/ConvexHullShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/ConvexShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/CylinderShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/DecoratedShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/EmptyShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/HeightFieldShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/MeshShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/MutableCompoundShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/PlaneShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/RotatedTranslatedShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/ScaledShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/Shape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/SphereShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/StaticCompoundShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/TaperedCapsuleShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/TaperedCylinderShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/Shape/TriangleShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Collision/TransformedShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/ConeConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/Constraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/ConstraintManager.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/ContactConstraintManager.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/DistanceConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/FixedConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/GearConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/HingeConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/MotorSettings.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/PathConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/PathConstraintPath.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/PathConstraintPathHermite.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/PointConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/PulleyConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/RackAndPinionConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/SixDOFConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/SliderConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/SpringSettings.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/SwingTwistConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Constraints/TwoBodyConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/DeterminismLog.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Hair/Hair.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Hair/HairSettings.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Hair/HairShaders.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Hair/RegisterHair.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/IslandBuilder.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/LargeIslandSplitter.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/PhysicsScene.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/PhysicsSystem.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/PhysicsUpdateContext.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Ragdoll/Ragdoll.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/SoftBody/SoftBodyCreationSettings.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/SoftBody/SoftBodyMotionProperties.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/SoftBody/SoftBodyShape.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/SoftBody/SoftBodySharedSettings.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/StateRecorderImpl.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/MotorcycleController.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/TrackedVehicleController.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleAntiRollBar.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleConstraint.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleController.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleDifferential.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleEngine.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleTrack.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/VehicleTransmission.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/Wheel.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Physics/Vehicle/WheeledVehicleController.cpp
    ${JOLT_SOURCE_DIR}/Jolt/RegisterTypes.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Renderer/DebugRenderer.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Renderer/DebugRendererPlayback.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Renderer/DebugRendererRecorder.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Renderer/DebugRendererSimple.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Shaders/HairWrapper.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Shaders/TestComputeWrapper.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Skeleton/SkeletalAnimation.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Skeleton/Skeleton.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Skeleton/SkeletonMapper.cpp
    ${JOLT_SOURCE_DIR}/Jolt/Skeleton/SkeletonPose.cpp
    ${JOLT_SOURCE_DIR}/Jolt/TriangleSplitter/TriangleSplitter.cpp
    ${JOLT_SOURCE_DIR}/Jolt/TriangleSplitter/TriangleSplitterBinning.cpp
    ${JOLT_SOURCE_DIR}/Jolt/TriangleSplitter/TriangleSplitterMean.cpp
)

# Jolt.h is included as <Jolt/...>, so the include root is the vendored
# tree itself (upstream uses PHYSICS_REPO_ROOT the same way).
target_include_directories(jolt SYSTEM PUBLIC
    ${JOLT_SOURCE_DIR}
)

set_target_properties(jolt PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# CXX_STANDARD above only compiles this static lib as C++17. Consumers that
# include <Jolt/...> (sk-jolt's jolt.cpp) must also be C++17 — MSVC's default
# is C++14, which has no std::string_view / std::align_val_t and fails both
# cl.exe and clang-tidy. PUBLIC compile features push /std:c++17 (or
# -std=c++17) onto every linking target.
target_compile_features(jolt PUBLIC cxx_std_17)

# The library uses std::thread/atomic; upstream threads the -pthread flags
# through to consumers on Unix (Jolt.cmake). Keep that contract so headers
# and the static lib stay ABI-consistent with JPH_USE_* defaults.
if(NOT WIN32)
    target_compile_options(jolt PUBLIC -pthread)
    target_link_options(jolt PUBLIC -pthread)
endif()
