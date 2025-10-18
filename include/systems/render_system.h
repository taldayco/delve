
#ifndef RENDER_SYSTEM_H
#define RENDER_SYSTEM_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

struct UISystem;
struct MapGenerator;
struct GLFWwindow;

struct RenderSystem {
  // Core Vulkan objects
  VkInstance instance{VK_NULL_HANDLE};
  VkDebugUtilsMessengerEXT debug_messenger{VK_NULL_HANDLE};
  VkSurfaceKHR surface{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device{VK_NULL_HANDLE};
  VkDevice device{VK_NULL_HANDLE};
  VkQueue graphics_queue{VK_NULL_HANDLE};
  VkQueue present_queue{VK_NULL_HANDLE};
  // Queue family indices
  uint32_t graphics_family{UINT32_MAX};
  uint32_t present_family{UINT32_MAX};
  // Swapchain state
  VkSwapchainKHR swapchain{VK_NULL_HANDLE};
  VkImage swapchain_images[8]{};          // Scratch space for images
  VkImageView swapchain_image_views[8]{}; // Views for rendering
  VkFormat swapchain_format{};
  VkExtent2D swapchain_extent{};
  uint32_t swapchain_image_count{0};
  // Render pass state
  VkRenderPass render_pass{VK_NULL_HANDLE};
  VkFramebuffer framebuffers[8]{};
  // create pool and buffers
  VkCommandPool command_pool{VK_NULL_HANDLE};
  VkCommandBuffer command_buffers[8]{};
  // Sync: Per-image semaphores, per-frame fences
  VkSemaphore image_available_semaphores[8]{}; // Per swapchain image
  VkSemaphore render_finished_semaphores[8]{}; // Per swapchain image
  VkFence in_flight_fences[2]{};               // Per frame (2 frames in flight)
  VkFence images_in_flight[8]{}; // Track which frame owns each image
  uint32_t current_frame{0};
  // shader modules
  VkShaderModule vertex_shader{VK_NULL_HANDLE};
  VkShaderModule fragment_shader{VK_NULL_HANDLE};
  // pipeline_layout
  VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
  VkPipeline graphics_pipeline{VK_NULL_HANDLE};
  // VMA and buffers
  VmaAllocator allocator{VK_NULL_HANDLE};
  // Circle buffers for map
  VkBuffer circle_vertex_buffer{VK_NULL_HANDLE};
  VmaAllocation circle_vertex_allocation{VK_NULL_HANDLE};
  uint32_t circle_vertex_count{0};
  // instance buffer
  VkBuffer instance_buffer{VK_NULL_HANDLE};
  VmaAllocation instance_allocation{VK_NULL_HANDLE};
  uint32_t instance_count{0};
  // Camera state
  float camera_x{100.0f}; // Center on map
  float camera_y{-200.0f};
  float camera_zoom{1.0f};
  // Window reference
  GLFWwindow *window{nullptr};
};
void render_init(RenderSystem *render, GLFWwindow *window,
                 const MapGenerator *map_gen);
void render_cleanup(RenderSystem *render);
void render_frame(RenderSystem *render, const UISystem *ui,
                  const MapGenerator *map_gen);

#endif
