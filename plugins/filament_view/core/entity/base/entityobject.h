/*
 * Copyright 2020-2024 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <encodable_value.h>
#include <mutex>
#include <string>
#include <vector>

#include <core/utils/filament_types.h>
#include <filament/Scene.h>

#include <core/components/base/component.h>
#include <core/utils/smarter_pointers.h>
#include <utility>

namespace plugin_filament_view {
class MaterialParameter;

// TODO: refactor to `EntityGUID` to `EntityId`
/// @brief EntityGUID is a type alias for the GUID of an entity (currently an
/// int64_t).
using EntityGUID = int64_t;

/// @brief kNullGuid is a constant that represents a null GUID.
constexpr EntityGUID kNullGuid = 0;

/// @brief EntityDescriptor is a struct that holds the name and guid of an
/// entity.
struct EntityDescriptor {
    std::string name = "";
    EntityGUID guid = kNullGuid;
};

class ECSManager;

/// @brief EntityObject is the base class for all entities in the system.
class EntityObject : public std::enable_shared_from_this<EntityObject> {
    friend class ECSManager;
    // TODO: do we need to expose these?
    friend class SceneTextDeserializer;

  protected:
    smarter_raw_ptr<ECSManager> ecs = nullptr;
    /// Temporary storage for components that are being added to the entity
    /// before it's initialized within ECS.
    /// After the entity is initialized, these components are moved to the ECS'
    /// memory and the temporary storage is cleared.
    std::map<TypeID, std::shared_ptr<Component>> _tmpComponents;

    /// GUID of the entity.
    /// This is used for identifying the entity, and must be unique.
    /// Also used as a key in the entity object locator system.
    EntityGUID guid_;

  public:
    FilamentEntity _fEntity;

    /// Name of the entity.
    /// This is used only for debugging and logging purposes.
    /// It is not used for identifying the entity, and does not need to be unique.
    std::string name;

    // Overloading the == operator to compare based on guid_
    bool operator==(const EntityObject& other) const { return guid_ == other.guid_; }

    [[nodiscard]] inline bool isInitialized() const { return _isInitialized; }

    /// @brief Checks if the entity is initialized. Throws an exception if not.
    /// @throws std::runtime_error if the entity is not initialized.
    void assertInitialized() const {
      // TODO: mutex?

      if (!_isInitialized) {
        throw std::runtime_error(
          // fmt::format("[{}] EntityObject ({}) is not initialized",
          // __FUNCTION__, guid_));
          "EntityObject is not initialized"
        );
      }
    }

    // TODO: remove this, change guid to const and expose as public
    [[nodiscard]] EntityGUID getGuid() const { return guid_; }

    EntityObject(const EntityObject&) = delete;
    EntityObject& operator=(const EntityObject&) = delete;

    virtual void debugPrint() const;

    /// @brief Constructor for EntityObject. Generates a GUID and has an empty
    /// name.
    explicit EntityObject();
    /// @brief Constructor for EntityObject with a name. Generates a unique GUID.
    explicit EntityObject(std::string name);
    /// @brief Constructor for EntityObject with GUID. Name is empty.
    explicit EntityObject(EntityGUID guid);
    /// @brief Constructor for EntityObject with a name and GUID.
    EntityObject(std::string name, EntityGUID guid);
    /// @brief Constructor for EntityObject based on an EntityDescriptor,
    /// containing a name and GUID.
    explicit EntityObject(const EntityDescriptor& descriptor);
    /// @brief Constructor for EntityObject based on an EncodableMap. Deserializes
    /// the name and GUID.
    explicit EntityObject(const flutter::EncodableMap& params);

    virtual ~EntityObject() = default;

    /// Called immediately after the entity is registered in the ECSManager.
    /// NOTE: this is a good place to initialize components and children.
    virtual void onInitialize();
    /// Called immediately before the entity is unregistered in the ECSManager.
    /// NOTE: it doesn't need to deallocate components or children, the ECSManager
    /// will do that.
    virtual void onDestroy() {};

  public:
    /// @brief Adds a component to the entity.
    /// If called before the entity is initialized, the component is batched
    /// and added to the entity after initialization.
    template<typename T> inline std::shared_ptr<T> addComponent(const T& component) {
      auto component = std::make_shared<T>(component);
      addComponent(T::StaticGetTypeID<T>(), component);
      return component;
    }

    /// Called by [ECSManager] when a component is added to the entity.
    /// This is called after the entity is initialized.
    /// If a component is added before the entity is initialized, this will be
    /// called after the entity is initialized.
    virtual void onAddComponent(const std::shared_ptr<Component>& component);

    template<typename T> [[nodiscard]] inline std::shared_ptr<T> getComponent() const {
      return std::dynamic_pointer_cast<T>(getComponent(Component::StaticGetTypeID<T>()));
    }

    template<typename T> [[nodiscard]] inline bool hasComponent() const {
      return hasComponent(Component::StaticGetTypeID<T>());
    }

    void debugPrintComponents() const;

    // finds the size_t staticTypeID in the component list
    // and creates a copy and assigns to the others list
    void ShallowCopyComponentToOther(size_t staticTypeID, EntityObject& other) const;

    static EntityDescriptor DeserializeNameAndGuid(const flutter::EncodableMap& params);

    /// @brief Deserializes the entity from a map of parameters.
    virtual void deserializeFrom(const flutter::EncodableMap& params);

  private:
    std::mutex _initMutex;
    bool _isInitialized = false;

    /// Called by ECSManager when the entity is registered.
    void initialize(ECSManager* ecsManager) {
      std::lock_guard lock(_initMutex);

      // Make sure it's not already initialized
      assert(_isInitialized == false);

      ecs = ecsManager;
      _isInitialized = true;
      onInitialize();
    }

    /// Called by ECSManager when the entity is unregistered.
    void uninitialize() {
      std::lock_guard lock(_initMutex);

      // Make sure it's not already uninitialized
      assert(_isInitialized == true);

      onDestroy();
      ecs = nullptr;
      _isInitialized = false;
    }

    void addComponent(TypeID staticTypeID, const std::shared_ptr<Component>& component);

    /// @brief Pass in the Component::StaticGetTypeID<DerivedClass>()
    /// @returns component if valid, nullptr if not found.
    [[nodiscard]] std::shared_ptr<Component> getComponent(TypeID staticTypeID) const;

    /// @brief Checks if the entity has a component of the given type.
    /// @param staticTypeID The static type ID of the component ->
    /// [Component::StaticGetTypeID<DerivedClass>()]
    /// @returns true if the entity has the component, false otherwise.
    [[nodiscard]] bool hasComponent(TypeID staticTypeID) const;
};
}  // namespace plugin_filament_view
