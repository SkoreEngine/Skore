#include <doctest.h>
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Component.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Allocator.hpp"

using namespace Skore;

#if 0

namespace
{
	// Static state tracking for testing
	namespace TestState
	{
		struct TrackingState
		{
			bool startCalled = false;
			int updateCount = 0;
		};
		
		struct PositionState
		{
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
		};
		
		struct DependentState
		{
			bool foundDependency = false;
			float sum = 0.0f;
		};
		
		struct SelfDestructState
		{
			int updateCount = 0;
			bool destroyed = false;
		};
		
		HashMap<UUID, TrackingState> trackingStates;
		HashMap<UUID, PositionState> positionStates;
		HashMap<UUID, DependentState> dependentStates;
		HashMap<UUID, SelfDestructState> selfDestructStates;
		
		void Reset()
		{
			trackingStates.Clear();
			positionStates.Clear();
			dependentStates.Clear();
			selfDestructStates.Clear();
		}
	}

	// ----- Test Components -----

	class TrackingComponent : public Component
	{
	public:
		SK_CLASS(TrackingComponent, Component);
		
		UUID stateId;
		
		TrackingComponent()
		{
			stateId = UUID::RandomUUID();
			TestState::trackingStates[stateId] = {};
		}

		void Start() override
		{
			auto& state = TestState::trackingStates[stateId];
			state.startCalled = true;
			EnableUpdate(true);
		}

		void Update(f64 deltaTime) override
		{
			auto& state = TestState::trackingStates[stateId];
			state.updateCount++;
		}
		
		bool IsStartCalled() const 
		{ 
			return TestState::trackingStates[stateId].startCalled; 
		}
		
		int GetUpdateCount() const 
		{ 
			return TestState::trackingStates[stateId].updateCount; 
		}

		static void RegisterType(NativeReflectType<TrackingComponent>& builder)
		{
			builder.Field<&TrackingComponent::stateId>("stateId");
		}
	};

	class PositionComponent : public Component
	{
	public:
		SK_CLASS(PositionComponent, Component);

		UUID stateId;
		
		PositionComponent()
		{
			stateId = UUID::RandomUUID();
			TestState::positionStates[stateId] = {};
		}

		void Start() override
		{
			EnableUpdate(true);
			auto& state = TestState::positionStates[stateId];
			state.x = state.y = state.z = 1.0f;
		}

		void Update(f64 deltaTime) override
		{
			auto& state = TestState::positionStates[stateId];
			state.x += 0.1f;
			state.y += 0.2f;
			state.z += 0.3f;
		}
		
		float GetX() const { return TestState::positionStates[stateId].x; }
		float GetY() const { return TestState::positionStates[stateId].y; }
		float GetZ() const { return TestState::positionStates[stateId].z; }
		
		void SetX(float value) { TestState::positionStates[stateId].x = value; }
		void SetY(float value) { TestState::positionStates[stateId].y = value; }
		void SetZ(float value) { TestState::positionStates[stateId].z = value; }

		static void RegisterType(NativeReflectType<PositionComponent>& builder)
		{
			builder.Field<&PositionComponent::stateId>("stateId");
		}
	};

	class DependentComponent : public Component
	{
	public:
		SK_CLASS(DependentComponent, Component);

		UUID stateId;
		
		DependentComponent()
		{
			stateId = UUID::RandomUUID();
			TestState::dependentStates[stateId] = {};
		}

		void Start() override
		{
			auto gameObj = GetEntity();
			Array<PositionComponent*> components = gameObj->GetComponents<PositionComponent>();
			
			auto& state = TestState::dependentStates[stateId];
			state.foundDependency = !components.Empty();

			EnableUpdate(true);
		}

		void Update(f64 deltaTime) override
		{
			auto components = GetEntity()->GetComponents<PositionComponent>();
			if (!components.Empty())
			{
				const auto& posComponent = components[0];
				auto& state = TestState::dependentStates[stateId];
				state.sum = posComponent->GetX() + posComponent->GetY() + posComponent->GetZ();
			}
		}
		
		bool IsFoundDependency() const { return TestState::dependentStates[stateId].foundDependency; }
		float GetSum() const { return TestState::dependentStates[stateId].sum; }

		static void RegisterType(NativeReflectType<DependentComponent>& builder)
		{
			builder.Field<&DependentComponent::stateId>("stateId");
		}
	};

	class SelfDestructComponent : public Component
	{
	public:
		SK_CLASS(SelfDestructComponent, Component);

		UUID stateId;
		int destroyOnUpdate = -1;
		
		SelfDestructComponent()
		{
			stateId = UUID::RandomUUID();
			TestState::selfDestructStates[stateId] = {};
		}

		void Start() override
		{
			EnableUpdate(true);
		}

		void Update(f64 deltaTime) override
		{
			auto& state = TestState::selfDestructStates[stateId];
			state.updateCount++;
			
			if (state.updateCount == destroyOnUpdate)
			{
				state.destroyed = true;
				GetEntity()->Destroy();
			}
		}
		
		int GetUpdateCount() const { return TestState::selfDestructStates[stateId].updateCount; }
		bool IsDestroyed() const { return TestState::selfDestructStates[stateId].destroyed; }

		static void RegisterType(NativeReflectType<SelfDestructComponent>& builder)
		{
			builder.Field<&SelfDestructComponent::stateId>("stateId");
			builder.Field<&SelfDestructComponent::destroyOnUpdate>("destroyOnUpdate");
		}
	};

	class NotificationComponent : public Component
	{
	public:
		SK_CLASS(NotificationComponent, Component);

		UUID stateId;
		bool startCalled = false;
		int updateCount = 0;
		Entity* targetToDestroy = nullptr;
		int destroyOnUpdate = -1;

		void Start() override
		{
			startCalled = true;
			EnableUpdate(true);
		}

		void Update(f64 deltaTime) override
		{
			updateCount++;
			
			if (updateCount == destroyOnUpdate && targetToDestroy)
			{
				targetToDestroy->Destroy();
				targetToDestroy = nullptr;
			}
		}

		static void RegisterType(NativeReflectType<NotificationComponent>& builder)
		{
			builder.Field<&NotificationComponent::startCalled>("startCalled");
			builder.Field<&NotificationComponent::updateCount>("updateCount");
			builder.Field<&NotificationComponent::destroyOnUpdate>("destroyOnUpdate");
		}
	};

	class CounterComponent : public Component
	{
	public:
		SK_CLASS(CounterComponent, Component);

		int value = 0;
		
		void Start() override
		{
			EnableUpdate(true);
		}
		
		void Update(f64 deltaTime) override
		{
			value++;
		}
		
		static void RegisterType(NativeReflectType<CounterComponent>& builder)
		{
			builder.Field<&CounterComponent::value>("value");
		}
	};

	void RegisterTypes()
	{
		Reflection::Type<TrackingComponent>();
		Reflection::Type<PositionComponent>();
		Reflection::Type<DependentComponent>();
		Reflection::Type<SelfDestructComponent>();
		Reflection::Type<NotificationComponent>();
		Reflection::Type<CounterComponent>();
	}

	// ----- Tests -----

	TEST_CASE("Scene lifecycle")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto  root = scene.GetRootEntity();

		CHECK(root != nullptr);
		CHECK(root->GetName() == "Root");
		CHECK(root->IsActive());
	}

	TEST_CASE("Entity creation and hierarchy")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto  root = scene.GetRootEntity();

		Entity* entity1 = Entity::Instantiate(root, "TestObject1");
		Entity* entity2 = Entity::Instantiate(entity1, "TestObject2");

		CHECK(entity1);
		CHECK(entity2);

		CHECK(entity1->GetName() == "TestObject1");
		CHECK(entity2->GetName() == "TestObject2");

		CHECK(entity1->GetScene() == &scene);
		CHECK(entity2->GetScene() == &scene);

		CHECK(root->HasChildren());
		CHECK(root->Children().Size() == 1);
		CHECK(root->Children()[0] == entity1);

		CHECK(entity1->HasChildren());
		CHECK(entity1->Children().Size() == 1);
		CHECK(entity1->Children()[0] == entity2);
	}

	TEST_CASE("Component lifecycle")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto  root = scene.GetRootEntity();
		auto  entity = Entity::Instantiate(root, "TestObject");

		CHECK(entity);

		auto component = entity->AddComponent<TrackingComponent>();

		CHECK(component != nullptr);
		CHECK_FALSE(component->IsStartCalled());
		CHECK(component->GetUpdateCount() == 0);
		
		UUID stateId = component->stateId;

		// Start should be called during the first update
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].startCalled);
		CHECK(TestState::trackingStates[stateId].updateCount == 1);

		// Update should be called on subsequent updates
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 2);
	}

	TEST_CASE("Component enable/disable update")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto  root = scene.GetRootEntity();
		auto  entity = Entity::Instantiate(root, "TestObject");

		CHECK(entity);

		auto component = entity->AddComponent<TrackingComponent>();
		UUID stateId = component->stateId;

		// First update calls Start and Update
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 1);

		// Disable updates
		component->EnableUpdate(false);
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 1); // Should not have increased

		// Re-enable updates
		component->EnableUpdate(true);
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 2);
	}

	TEST_CASE("Entity active state")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto  root = scene.GetRootEntity();

		auto  entity = Entity::Instantiate(root, "TestObject");
		CHECK(entity);

		auto component = entity->AddComponent<TrackingComponent>();
		UUID stateId = component->stateId;

		// First update calls Start and Update
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 1);

		// Deactivate Entity
		entity->SetActive(false);
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 1); // Should not have increased

		// Reactivate Entity
		entity->SetActive(true);
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 2);
	}

	TEST_CASE("Component dependencies")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto  root = scene.GetRootEntity();
		auto  entity = Entity::Instantiate(root, "TestObject");
		CHECK(entity);

		// Add position component first
		auto position = entity->AddComponent<PositionComponent>();
		auto dependent = entity->AddComponent<DependentComponent>();
		
		UUID positionStateId = position->stateId;
		UUID dependentStateId = dependent->stateId;

		// First update triggers Start for both components
		scene.Update(0.016);

		CHECK(TestState::dependentStates[dependentStateId].foundDependency);

		// Second update should have updated values
		scene.Update(0.016);

		CHECK(TestState::positionStates[positionStateId].x == doctest::Approx(1.2f));
		CHECK(TestState::positionStates[positionStateId].y == doctest::Approx(1.4f));
		CHECK(TestState::positionStates[positionStateId].z == doctest::Approx(1.6f));
		CHECK(TestState::dependentStates[dependentStateId].sum == doctest::Approx(4.2f));
	}

	TEST_CASE("Component removal")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto  root = scene.GetRootEntity();
		auto  entity = Entity::Instantiate(root, "TestObject");
		CHECK(entity);

		auto component = entity->AddComponent<TrackingComponent>();
		UUID stateId = component->stateId;

		// Update once to initialize component
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 1);

		// Remove the component
		entity->RemoveComponent(component);

		// Update again - the count should not increase since component was removed
		scene.Update(0.016);
		CHECK(TestState::trackingStates[stateId].updateCount == 1);
	}

	TEST_CASE("Reflection system for components")
	{
		TestState::Reset();
		RegisterTypes();

		// Verify components are registered with reflection system
		auto trackingType = Reflection::FindType<TrackingComponent>();
		auto positionType = Reflection::FindType<PositionComponent>();
		auto dependentType = Reflection::FindType<DependentComponent>();

		CHECK(trackingType != nullptr);
		CHECK(positionType != nullptr);
		CHECK(dependentType != nullptr);

		// Check field names were properly registered
		CHECK(trackingType->FindField("stateId") != nullptr);
		
		CHECK(positionType->FindField("stateId") != nullptr);
		
		CHECK(dependentType->FindField("stateId") != nullptr);

		// Create component using reflection
		Scene scene;
		auto  root = scene.GetRootEntity();
		auto  entity = Entity::Instantiate(root, "TestObject");
		CHECK(entity);

		auto component = entity->AddComponent(TypeInfo<TrackingComponent>::ID());
		CHECK(component != nullptr);

		auto typedComponent = static_cast<TrackingComponent*>(component);
		CHECK(typedComponent != nullptr);
	}

	TEST_CASE("Entity destruction")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		Entity* parent = Entity::Instantiate(root, "Parent");
		Entity* child = Entity::Instantiate(parent, "Child");
		
		CHECK(parent);
		CHECK(child);
		CHECK(root->Children().Size() == 1);
		CHECK(parent->Children().Size() == 1);
		
		// Add tracking component to child to verify it's properly updated
		auto childComponent = child->AddComponent<TrackingComponent>();
		UUID childStateId = childComponent->stateId;
		
		// First update to initialize components
		scene.Update(0.016);
		CHECK(TestState::trackingStates[childStateId].updateCount == 1);
		
		// Destroy the parent
		parent->Destroy();
		
		// Next update should process the destruction
		scene.Update(0.016);
		
		// Root should have no more children
		CHECK(root->Children().Size() == 0);
		
		// Components won't be updated anymore since they're destroyed
		scene.Update(0.016);
		CHECK(TestState::trackingStates[childStateId].updateCount == 1); // No further updates
	}

	TEST_CASE("Entity self-destruction from component")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		Entity* entity = Entity::Instantiate(root, "SelfDestruct");
		CHECK(entity);
		
		// Add a component that will destroy its own Entity
		auto component = entity->AddComponent<SelfDestructComponent>();
		component->destroyOnUpdate = 2; // Destroy on second update
		UUID stateId = component->stateId;
		
		// First update - initialization
		scene.Update(0.016);
		CHECK(TestState::selfDestructStates[stateId].updateCount == 1);
		CHECK(root->Children().Size() == 1);
		
		// Second update - should destroy itself
		scene.Update(0.016);
		CHECK(TestState::selfDestructStates[stateId].destroyed == true);
		
		// Third update - destruction should be processed
		scene.Update(0.016);
		CHECK(root->Children().Size() == 0);
	}

	TEST_CASE("Entity destruction with multiple components")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		Entity* entity = Entity::Instantiate(root, "MultiComponent");
		CHECK(entity);
		
		// Add multiple components to track behavior during destruction
		auto tracking = entity->AddComponent<TrackingComponent>();
		auto position = entity->AddComponent<PositionComponent>();
		auto selfDestruct = entity->AddComponent<SelfDestructComponent>();
		selfDestruct->destroyOnUpdate = 2;
		
		UUID trackingStateId = tracking->stateId;
		UUID positionStateId = position->stateId;
		UUID selfDestructStateId = selfDestruct->stateId;
		
		// First update - initialization
		scene.Update(0.016);
		CHECK(TestState::trackingStates[trackingStateId].updateCount == 1);
		CHECK(TestState::positionStates[positionStateId].x == doctest::Approx(1.1f));
		CHECK(TestState::selfDestructStates[selfDestructStateId].updateCount == 1);
		
		// Second update - should destroy itself
		scene.Update(0.016);
		CHECK(TestState::selfDestructStates[selfDestructStateId].destroyed == true);
		
		// Third update - destruction should be processed
		scene.Update(0.016);
		CHECK(root->Children().Size() == 0);
		
		// No further updates to components since they're destroyed
		scene.Update(0.016);
		CHECK(TestState::trackingStates[trackingStateId].updateCount == 2); // No increase from last valid update
	}

	TEST_CASE("Entity hierarchy destruction")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		Entity* parent = Entity::Instantiate(root, "Parent");
		Entity* child1 = Entity::Instantiate(parent, "Child1");
		Entity* child2 = Entity::Instantiate(parent, "Child2");
		Entity* grandchild = Entity::Instantiate(child1, "Grandchild");
		
		CHECK(parent->Children().Size() == 2);
		CHECK(child1->Children().Size() == 1);
		
		// Add tracking components to verify update behavior
		auto track1 = child1->AddComponent<TrackingComponent>();
		auto track2 = child2->AddComponent<TrackingComponent>();
		auto trackGrand = grandchild->AddComponent<TrackingComponent>();
		
		UUID track1Id = track1->stateId;
		UUID track2Id = track2->stateId;
		UUID trackGrandId = trackGrand->stateId;
		
		// First update - initialization
		scene.Update(0.016);
		CHECK(TestState::trackingStates[track1Id].updateCount == 1);
		CHECK(TestState::trackingStates[track2Id].updateCount == 1);
		CHECK(TestState::trackingStates[trackGrandId].updateCount == 1);
		
		// Set up a component to destroy the parent
		auto notification = root->AddComponent<NotificationComponent>();
		notification->targetToDestroy = parent;
		notification->destroyOnUpdate = 2;

		// Second update
		scene.Update(0.016);
		
		// Third update - should queue parent for destruction
		scene.Update(0.016);
		
		// Forth update - destruction should be processed
		scene.Update(0.016);
		CHECK(root->Children().Size() == 0);

		// Components should still have the last update count before destruction
		CHECK(TestState::trackingStates[track1Id].updateCount == 3);
		CHECK(TestState::trackingStates[track2Id].updateCount == 3);
		CHECK(TestState::trackingStates[trackGrandId].updateCount == 3);
		
		// No further updates to any of the child components
		scene.Update(0.016);
		CHECK(TestState::trackingStates[track1Id].updateCount == 3);
		CHECK(TestState::trackingStates[track2Id].updateCount == 3);
		CHECK(TestState::trackingStates[trackGrandId].updateCount == 3);
	}

	TEST_CASE("Entity basic duplication")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		// Create original Entity
		Entity* original = Entity::Instantiate(root, "Original");
		CHECK(original);
		
		// Add a component to the original
		auto component = original->AddComponent<CounterComponent>();
		
		// Perform the duplication
		Entity* duplicate = original->Duplicate();
		CHECK(duplicate);
		
		// Check basic properties match
		CHECK(duplicate->GetName() == original->GetName());
		CHECK(duplicate->GetParent() == original->GetParent());
		CHECK(duplicate != original); // Not the same object reference
		
		// Check parent-child relationship
		CHECK(root->Children().Size() == 2); // Both original and duplicate are children of root
		
		// Start update to initialize components
		scene.Update(0.016);
		
		// Components should be independent
		auto duplicateComponents = duplicate->GetComponents<CounterComponent>();
		CHECK(duplicateComponents.Size() == 1);
		
		auto duplicateComponent = duplicateComponents[0];
		CHECK(duplicateComponent->value == component->value);
		
		// Modify original component's value
		component->value = 10;
		
		// Duplicate's component should remain unchanged
		CHECK(duplicateComponent->value == 1);
		
		// Both objects should be updated independently
		scene.Update(0.016);
		CHECK(component->value == 11);
		CHECK(duplicateComponent->value == 2);
	}

	TEST_CASE("Entity duplication with hierarchy")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		// Create original hierarchy
		Entity* parent = Entity::Instantiate(root, "Parent");
		Entity* child1 = Entity::Instantiate(parent, "Child1");
		Entity* child2 = Entity::Instantiate(parent, "Child2");
		
		// Add components
		auto parentComponent = parent->AddComponent<CounterComponent>();
		auto child1Component = child1->AddComponent<CounterComponent>();
		auto child2Component = child2->AddComponent<PositionComponent>();
		
		// Duplicate the parent with its hierarchy
		Entity* duplicateParent = parent->Duplicate();
		CHECK(duplicateParent);
		
		// Check the hierarchy was duplicated
		CHECK(duplicateParent->Children().Size() == 2);
		CHECK(root->Children().Size() == 2); // Original parent and duplicate parent
		
		// Get duplicated children
		Entity* duplicateChild1 = duplicateParent->Children()[0];
		Entity* duplicateChild2 = duplicateParent->Children()[1];
		
		// Check child names were duplicated
		CHECK(duplicateChild1->GetName() == child1->GetName());
		CHECK(duplicateChild2->GetName() == child2->GetName());
		
		// Check components were duplicated
		auto duplicateParentComponents = duplicateParent->GetComponents<CounterComponent>();
		CHECK(duplicateParentComponents.Size() == 1);
		
		auto duplicateChild1Components = duplicateChild1->GetComponents<CounterComponent>();
		CHECK(duplicateChild1Components.Size() == 1);
		
		auto duplicateChild2Components = duplicateChild2->GetComponents<PositionComponent>();
		CHECK(duplicateChild2Components.Size() == 1);
		
		// First update to initialize components
		scene.Update(0.016);
		
		// Check components are initialized properly
		CHECK(parentComponent->value == 1);
		CHECK(duplicateParentComponents[0]->value == 1);
		
		// Second update to verify both hierarchies update independently
		scene.Update(0.016);
		
		CHECK(parentComponent->value == 2);
		CHECK(duplicateParentComponents[0]->value == 2);
		CHECK(child1Component->value == 2);
		CHECK(duplicateChild1Components[0]->value == 2);
		
		UUID positionStateId = child2Component->stateId;
		UUID duplicatePosStateId = duplicateChild2Components[0]->stateId;
		
		// Check position component values
		CHECK(TestState::positionStates[positionStateId].x == doctest::Approx(1.2f));
		CHECK(TestState::positionStates[duplicatePosStateId].x == doctest::Approx(1.2f));
		
		// Modify one hierarchy and verify the other is unaffected
		parentComponent->value = 10;
		child2Component->SetX(5.0f);
		
		CHECK(duplicateParentComponents[0]->value == 2);
		CHECK(TestState::positionStates[duplicatePosStateId].x == doctest::Approx(1.2f));
	}

	TEST_CASE("Entity duplication with new parent")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		// Create original parent-child structure
		Entity* parentA = Entity::Instantiate(root, "ParentA");
		Entity* childA = Entity::Instantiate(parentA, "ChildA");
		Entity* parentB = Entity::Instantiate(root, "ParentB");
		
		// Add components
		auto componentA = childA->AddComponent<CounterComponent>();
		
		// Duplicate childA to new parent
		Entity* duplicatedChild = childA->Duplicate(parentB);
		CHECK(duplicatedChild);
		
		// Check parent-child relationships
		CHECK(parentA->Children().Size() == 1);
		CHECK(parentB->Children().Size() == 1);
		CHECK(duplicatedChild->GetParent() == parentB);
		
		// Check components were duplicated
		auto duplicatedComponents = duplicatedChild->GetComponents<CounterComponent>();
		CHECK(duplicatedComponents.Size() == 1);
		
		// First update to initialize components
		scene.Update(0.016);
		
		// Verify components work independently
		CHECK(componentA->value == 1);
		CHECK(duplicatedComponents[0]->value == 1);
		
		// Second update
		scene.Update(0.016);
		
		CHECK(componentA->value == 2);
		CHECK(duplicatedComponents[0]->value == 2);
		
		// Deactivate original parent
		parentA->SetActive(false);
		scene.Update(0.016);
		
		// Original child component shouldn't update, but duplicate should
		CHECK(componentA->value == 2); // Unchanged
		CHECK(duplicatedComponents[0]->value == 3); // Still updating
	}

	TEST_CASE("Duplicated Entity with component dependencies")
	{
		TestState::Reset();
		RegisterTypes();

		Scene scene;
		auto root = scene.GetRootEntity();
		
		// Create original Entity with dependent components
		Entity* original = Entity::Instantiate(root, "Original");
		auto position = original->AddComponent<PositionComponent>();
		auto dependent = original->AddComponent<DependentComponent>();
		
		UUID positionStateId = position->stateId;
		UUID dependentStateId = dependent->stateId;
		
		// Duplicate the Entity
		Entity* duplicate = original->Duplicate();
		
		// First update to initialize components
		scene.Update(0.016);
		
		// Check both objects' dependencies work properly
		auto duplicatePositions = duplicate->GetComponents<PositionComponent>();
		CHECK(duplicatePositions.Size() == 1);
		
		auto duplicateDependents = duplicate->GetComponents<DependentComponent>();
		CHECK(duplicateDependents.Size() == 1);
		
		UUID duplicatePosStateId = duplicatePositions[0]->stateId;
		UUID duplicateDepStateId = duplicateDependents[0]->stateId;
		
		// Verify dependency resolution in duplicate
		CHECK(TestState::dependentStates[dependentStateId].foundDependency == true);
		CHECK(TestState::dependentStates[duplicateDepStateId].foundDependency == true);
		
		// Second update to verify values
		scene.Update(0.016);
		
		// Check both position components update independently
		CHECK(TestState::positionStates[positionStateId].x == doctest::Approx(1.2f));
		CHECK(TestState::positionStates[duplicatePosStateId].x == doctest::Approx(1.2f));
		
		// Check dependency calculation
		CHECK(TestState::dependentStates[dependentStateId].sum == doctest::Approx(4.2f));
		CHECK(TestState::dependentStates[duplicateDepStateId].sum == doctest::Approx(4.2f));
		
		// Modify original and check duplicate remains unchanged
		position->SetX(10.0f);
		position->SetY(20.0f);
		position->SetZ(30.0f);
		
		// Force another update to recalculate dependent values
		scene.Update(0.016);
		
		// Duplicate should remain unchanged from normal progression
		CHECK(TestState::positionStates[duplicatePosStateId].x == doctest::Approx(1.3f));
		CHECK(TestState::positionStates[duplicatePosStateId].y == doctest::Approx(1.6f));
		CHECK(TestState::positionStates[duplicatePosStateId].z == doctest::Approx(1.9f));
		
		// Dependent component values should reflect their own position components
		CHECK(TestState::dependentStates[dependentStateId].sum == doctest::Approx(60.6f));
		CHECK(TestState::dependentStates[duplicateDepStateId].sum == doctest::Approx(4.8f)); // 1.3 + 1.6 + 1.9
	}
}

#endif
