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

#include <asio/io_context_strand.hpp>
#include <core/systems/base/system.h>
#include <future>
#include <map>
#include <memory>
#include <shared_mutex>
#include <spdlog/spdlog.h>
#include <vector>

#include <core/entity/base/entityobject.h>
#include <core/utils/asserts.h>
#include <core/utils/kvtree.h>

#define CRASH_ON_INIT  // if true, will not continue if any System fails to
                       // init

namespace plugin_filament_view {

/// Enum indicating a ECS CRUD operation
enum class ECSOperation {
  /// @brief When a new entity/component/system is being added
  Add,
  /// @brief When an existing entity/component/system is being removed
  Remove
};

const std::string& ECSOperationToString(ECSOperation operation);

class ECSManager {
  public:
    enum RunState { NotInitialized, Initialized, Running, ShutdownStarted, Shutdown };

  private:
    std::map<std::string, std::any> m_mapConfigurationValues;

    void setupThreadingInternals();

    void MainLoop();

    //
    //  Entity
    //
    std::mutex _entitiesMutex;
    KVTree<EntityGUID, std::shared_ptr<EntityObject>> _entities;

    //
    //  Component
    //

    std::mutex _componentsMutex;
    /// Map of component type IDs to their corresponding entityID->component maps
    std::map<TypeID, std::map<EntityGUID, std::shared_ptr<Component>>> _components;

    //
    //  System
    //

    std::mutex _systemsMutex;
    std::map<TypeID, std::shared_ptr<System>> _systems;

    /// Maps component types to system IDs listening for their addition
    std::unordered_map<TypeID, std::vector<TypeID>> _componentListeners;

    /// Notifies a system about an [ECSOperation] being performed on a component
    void _notifyComponentOperation(
      EntityObject& entity,
      Component& component,
      ECSOperation operation
    );

    //
    // Threading
    //
    std::atomic<bool> m_bIsRunning{false};
    std::atomic<bool> m_bSpawnedThreadFinished{false};
    std::atomic<bool> isHandlerExecuting{false};

    std::thread filament_api_thread_;
    pthread_t filament_api_thread_id_{};
    std::unique_ptr<asio::io_context> io_context_;
    asio::executor_work_guard<decltype(io_context_->get_executor())> work_;
    std::unique_ptr<asio::io_context::strand> strand_;
    std::thread loopThread_;

    std::map<std::string, int> m_mapOffThreadCallers;

    RunState m_eCurrentState;
    static ECSManager* m_poInstance;

    ECSManager();
    ~ECSManager();

  public:
    [[nodiscard]] inline RunState getRunState() const { return m_eCurrentState; }

    [[nodiscard]] inline static ECSManager* GetInstance() {
      if (m_poInstance == nullptr) {
        m_poInstance = new ECSManager();
      }
      return m_poInstance;
    }

    ECSManager(const ECSManager&) = delete;
    ECSManager& operator=(const ECSManager&) = delete;

    void initialize();

    /**
     * @brief Updates the engine logic for the current frame.
     *
     * It's called once per frame and is responsible for updating
     * all engine entities, systems, and logic based on the elapsed time since
     * the last frame.
     *
     * NOTE: must be run on the main thread.
     *
     * @param deltaTime The time elapsed since the last frame, in seconds.
     *                  This value should be used to make all movement and
     *                  time-based calculations frame rate independent.
     */
    void update(double deltaTime);
    void destroy();

    void debugPrint() const;

    void StartMainLoop();
    void StopMainLoop();

    [[nodiscard]] bool bIsCompletedStopping() const { return m_bSpawnedThreadFinished; }

    //
    //  Entity
    //
    void addEntity(const std::shared_ptr<EntityObject>& entity, const EntityGUID* parent = nullptr);
    void removeEntity(const EntityGUID entityGuid);
    [[nodiscard]] std::shared_ptr<EntityObject> getEntity(EntityGUID id);

    /// Moves the entity with the given GUID to the parent with the given GUID.
    void reparentEntity(const std::shared_ptr<EntityObject>& entity, const EntityGUID& parentGuid);
    /// Returns the children of the entity with the given GUID.
    [[nodiscard]] std::vector<std::shared_ptr<EntityObject>> getEntityChildren(EntityGUID id);
    /// Returns the GUIDs of the children of the entity with the given GUID.
    [[nodiscard]] std::vector<EntityGUID> getEntityChildrenGuids(EntityGUID id);

    /// Returns the parent of the entity with the given GUID.
    [[nodiscard]] std::shared_ptr<EntityObject> getEntityParent(EntityGUID id);
    /// Returns the GUID of the parent of the entity with the given GUID.
    [[nodiscard]] std::optional<EntityGUID> getEntityParentGuid(EntityGUID id);

    /// Returns all enttities having a component of the given type.
    template<typename T>
    [[nodiscard]] std::vector<std::shared_ptr<EntityObject>> getEntitiesWithComponent() {
      return getEntitiesWithComponent(Component::StaticGetTypeID<T>());
    }

    [[nodiscard]] std::vector<std::shared_ptr<EntityObject>> getEntitiesWithComponent(
      TypeID componentTypeId
    );

    /// Checks whether the entity with the given GUID exists.
    /// @throws std::runtime_error if the entity does not exist.
    void checkHasEntity(EntityGUID id);

    //
    //  Component
    //

    /// Adds a component to the entity with the given GUID.
    void addComponent(const EntityGUID entityGuid, const std::shared_ptr<Component>& component);

    /// Removes a component from the entity with the given GUID.
    /// If either the entity or the component does not exist, nothing happens.
    template<typename T> inline void removeComponent(const EntityGUID& entityGuid) {
      removeComponent(entityGuid, Component::StaticGetTypeID<T>());
    }

    void removeComponent(const EntityGUID& entityGuid, TypeID componentTypeId);

    /// Returns the component of the given type from the entity with the given
    /// GUID.
    template<typename T>
    [[nodiscard]] inline std::shared_ptr<T> getComponent(const EntityGUID& entityGuid) {
      return std::dynamic_pointer_cast<T>(getComponent(entityGuid, Component::StaticGetTypeID<T>())
      );
    }

    [[nodiscard]] std::shared_ptr<Component> getComponent(
      const EntityGUID& entityGuid,
      TypeID componentTypeId
    );

    /// Returns all the components of the given type
    template<typename T>
    [[nodiscard]] inline std::vector<std::shared_ptr<T>> getComponentsOfType() {
      // cast vector type
      /// TODO: this does a memcopy, nuh-uh
      std::vector<std::shared_ptr<T>> components;
      auto tmp = getComponentsOfType(Component::StaticGetTypeID<T>());
      components.reserve(tmp.size());
      for (const auto& component : tmp) {
        components.push_back(std::dynamic_pointer_cast<T>(component));
      }
      return components;
    }

    [[nodiscard]] std::vector<std::shared_ptr<Component>> getComponentsOfType(TypeID componentTypeId
    );

    /// Returns whether the entity with the given GUID has a component of the
    /// given type.
    template<typename T> [[nodiscard]] inline bool hasComponent(const EntityGUID& entityGuid) {
      return hasComponent(entityGuid, Component::StaticGetTypeID<T>());
    }

    [[nodiscard]] bool hasComponent(const EntityGUID entityGuid, TypeID componentTypeId);

    /// Returns a vector of pointers to all components of the entity with the
    /// given GUID.
    [[nodiscard]] std::vector<std::shared_ptr<Component>> getComponentsOfEntity(
      const EntityGUID& entityGuid
    );

    //
    //  System
    //
    template<typename T>
    [[nodiscard]] inline std::shared_ptr<T> getSystem(const std::string& where) {
      return std::dynamic_pointer_cast<T>(getSystem(System::StaticGetTypeID<T>(), where));
    }

    [[nodiscard]] std::shared_ptr<System> getSystem(TypeID systemTypeID, const std::string& where);
    void addSystem(const std::shared_ptr<System>& system);

    template<typename T> inline void removeSystem() { removeSystem(System::StaticGetTypeID<T>()); };

    void removeSystem(TypeID systemTypeId);

    /**
     * Send a message to all registered systems
     * \deprecated Deprecated in favor of queueTask
     */
    void RouteMessage(const ECSMessage& msg) {
      std::unique_lock<std::mutex> lock(_systemsMutex);
      // for (const auto& system : _systems) {
      //   system->SendMessage(msg);
      // }

      // _systems is a map
      for (auto& [_, system] : _systems) {
        system->SendMessage(msg);
      }
    }

    /// Registers a system as a listener for when
    /// a specific component type is added/removed to an entity
    void registerComponentListener(
      const System& listener,
      const TypeID componentTypeId
    );

    //
    //  Threading
    //

    [[nodiscard]] inline pthread_t getFilamentAPIThreadID() const {
      return filament_api_thread_id_;
    }

    [[nodiscard]] inline const std::thread& getFilamentAPIThread() const {
      return filament_api_thread_;
    }

    [[nodiscard]] inline std::unique_ptr<asio::io_context::strand>& getStrand() { return strand_; }

    //
    //  Global state (configuration)
    //

    template<typename T> void inline setConfigValue(const std::string& key, T value) {
      m_mapConfigurationValues[key] = value;
    }

    // getter for any type of value
    template<typename T> T getConfigValue(const std::string& key) const {
      auto it = m_mapConfigurationValues.find(key);
      if (it != m_mapConfigurationValues.end()) {
        try {
          return std::any_cast<T>(it->second);  // Cast the value to the expected type
        } catch (const std::bad_any_cast& e) {
          throw std::runtime_error("Error: Incorrect type for key: " + key);
        }
      } else {
        throw std::runtime_error("Error: Key not found: " + key);
      }
    }
};
}  // namespace plugin_filament_view
