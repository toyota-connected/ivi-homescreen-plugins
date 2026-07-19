/*
 * Copyright 2026 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#include "external_headless_backend.h"

#include <mbgl/gfx/backend_scope.hpp>
#include <mbgl/vulkan/context.hpp>
#include <mbgl/vulkan/renderable_resource.hpp>

namespace plugin_maplibre_view {

// Surfaceless renderable: no VkSurfaceKHR, so SurfaceRenderableResource::init()
// takes the initColor() branch and allocates a plain color VkImage (the one the
// compositor samples). Copied from mbgl::vulkan::HeadlessBackend's internal
// resource (which is not exported).
class ExternalRenderableResource final
    : public mbgl::vulkan::SurfaceRenderableResource {
 public:
  explicit ExternalRenderableResource(ExternalHeadlessBackend& backend_)
      : SurfaceRenderableResource(backend_) {
    if (backend_.getDevice() && !renderPass) {
      init(backend_.getSize().width, backend_.getSize().height);
    }
  }
  ~ExternalRenderableResource() noexcept override = default;

  void createPlatformSurface() override {}
  void bind() override {}

  void swap() override {
    SurfaceRenderableResource::swap();
    // static_cast (not dynamic_cast): mbgl-core is built -fno-rtti, so a
    // dynamic_cast here fails to compile. Matches mbgl's own headless resource.
    static_cast<mbgl::vulkan::Context&>(backend.getContext()).waitFrame();
  }
};

ExternalHeadlessBackend::ExternalHeadlessBackend(
    const mbgl::vulkan::RendererBackend::ExternalVulkanContext& ctx,
    const mbgl::Size size)
    : mbgl::vulkan::RendererBackend(mbgl::gfx::ContextMode::Unique),
      mbgl::gfx::HeadlessBackend(size) {
  // Adopt the compositor's device instead of init(). initExternal skips
  // instance/device/swapchain creation and leaves ownsVulkan=false so teardown
  // releases (never destroys) the host's instance/device.
  initExternal(ctx);
}

ExternalHeadlessBackend::~ExternalHeadlessBackend() {
  mbgl::gfx::BackendScope guard{*this,
                                mbgl::gfx::BackendScope::ScopeType::Implicit};
  // Reset the renderable + drain render jobs before the context so GPU work
  // referencing the (borrowed) device completes while it is still valid.
  resource.reset();
  getThreadPool().runRenderJobs(true /* closeQueue */);
  context.reset();
}

void ExternalHeadlessBackend::activate() {
  active_ = true;
}

void ExternalHeadlessBackend::deactivate() {
  active_ = false;
}

mbgl::gfx::Renderable& ExternalHeadlessBackend::getDefaultRenderable() {
  if (!resource) {
    resource = std::make_unique<ExternalRenderableResource>(*this);
  }
  return *this;
}

mbgl::PremultipliedImage ExternalHeadlessBackend::readStillImage() {
  // Not used: the compositor samples colorImage() directly (zero-copy). A CPU
  // readback would defeat the purpose, so this returns an empty image.
  return {};
}

mbgl::vulkan::RendererBackend* ExternalHeadlessBackend::getRendererBackend() {
  return this;
}

VkImage ExternalHeadlessBackend::colorImage() {
  auto& res = getDefaultRenderable()
                  .getResource<mbgl::vulkan::SurfaceRenderableResource>();
  return static_cast<VkImage>(res.getAcquiredImage());
}

}  // namespace plugin_maplibre_view
