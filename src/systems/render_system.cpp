#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "systems/render_system.h"
#include "map/map_generator.h"
#include "systems/ui_system.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#include <vk_mem_alloc.h>
// ---- Validation Layers ----
#ifdef VK_ENABLE_VALIDATION
static const char *VALIDATION_LAYERS[] = {"VK_LAYER_KHRONOS_validation"};
static constexpr uint32_t VALIDATION_LAYER_COUNT = 1;
#else
static constexpr uint32_t VALIDATION_LAYER_COUNT = 0;
#endif
// ---- Data Structures ----
struct PushConstants {
  float view_projection[16]; // 4x4 matrix
};

struct Vertex {
  float pos[2];
};

struct InstanceData {
  float position[2];
  float color[4];
  float scale;
};
// ---- Debug Callback ----
#ifdef VK_ENABLE_VALIDATION
static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
               VkDebugUtilsMessageTypeFlagsEXT type,
               const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
               void *user_data) {
  (void)type;
  (void)user_data;

  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::cerr << "[Vulkan] " << callback_data->pMessage << "\n";
  }
  return VK_FALSE;
}

static VkResult create_debug_messenger(VkInstance instance,
                                       VkDebugUtilsMessengerEXT *messenger) {
  VkDebugUtilsMessengerCreateInfoEXT create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = debug_callback;

  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");

  if (func) {
    return func(instance, &create_info, nullptr, messenger);
  }
  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroy_debug_messenger(VkInstance instance,
                                    VkDebugUtilsMessengerEXT messenger) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func) {
    func(instance, messenger, nullptr);
  }
}
#endif

// ---- Instance Creation ----
static bool create_instance(RenderSystem *render) {
  // Application info
  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Delve Map Generator";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;

  // Get required extensions from GLFW
  uint32_t glfw_extension_count = 0;
  const char **glfw_extensions =
      glfwGetRequiredInstanceExtensions(&glfw_extension_count);

  // Build extension list (add debug utils if validation enabled)
  std::vector<const char *> extensions(glfw_extensions,
                                       glfw_extensions + glfw_extension_count);
#ifdef VK_ENABLE_VALIDATION
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

  // Instance create info
  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();

#ifdef VK_ENABLE_VALIDATION
  create_info.enabledLayerCount = VALIDATION_LAYER_COUNT;
  create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
  std::cout << "[Vulkan] Validation layers enabled\n";
#else
  create_info.enabledLayerCount = 0;
#endif

  VkResult result = vkCreateInstance(&create_info, nullptr, &render->instance);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create instance: " << result << "\n";
    return false;
  }

  std::cout << "[Vulkan] Instance created\n";
  return true;
}
// ---- Queue Family Finding ----
struct QueueFamilyIndices {
  uint32_t graphics{UINT32_MAX};
  uint32_t present{UINT32_MAX};

  bool is_complete() const {
    return graphics != UINT32_MAX && present != UINT32_MAX;
  }
};

static QueueFamilyIndices find_queue_families(VkPhysicalDevice device,
                                              VkSurfaceKHR surface) {
  QueueFamilyIndices indices;

  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
                                           nullptr);

  VkQueueFamilyProperties families[32]; // Scratch space
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
                                           families);

  for (uint32_t i = 0; i < queue_family_count; ++i) {
    // Graphics support
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphics = i;
    }

    // Present support
    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
    if (present_support) {
      indices.present = i;
    }

    if (indices.is_complete())
      break;
  }

  return indices;
}

// ---- Physical Device Selection ----
static bool is_device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
  QueueFamilyIndices indices = find_queue_families(device, surface);

  // Check for swapchain extension support
  uint32_t extension_count;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                       nullptr);

  VkExtensionProperties available_extensions[256];
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                       available_extensions);

  bool swapchain_supported = false;
  for (uint32_t i = 0; i < extension_count; ++i) {
    if (strcmp(available_extensions[i].extensionName,
               VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
      swapchain_supported = true;
      break;
    }
  }

  return indices.is_complete() && swapchain_supported;
}

static bool pick_physical_device(RenderSystem *render) {
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(render->instance, &device_count, nullptr);

  if (device_count == 0) {
    std::cerr << "[Vulkan] No GPUs with Vulkan support found\n";
    return false;
  }

  VkPhysicalDevice devices[8]; // Scratch space
  vkEnumeratePhysicalDevices(render->instance, &device_count, devices);

  // Prefer discrete GPU
  VkPhysicalDevice discrete_gpu = VK_NULL_HANDLE;
  VkPhysicalDevice any_suitable = VK_NULL_HANDLE;

  for (uint32_t i = 0; i < device_count; ++i) {
    if (is_device_suitable(devices[i], render->surface)) {
      any_suitable = devices[i];

      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(devices[i], &props);

      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        discrete_gpu = devices[i];
        break;
      }
    }
  }

  render->physical_device = discrete_gpu ? discrete_gpu : any_suitable;

  if (render->physical_device == VK_NULL_HANDLE) {
    std::cerr << "[Vulkan] No suitable GPU found\n";
    return false;
  }

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(render->physical_device, &props);
  std::cout << "[Vulkan] Selected GPU: " << props.deviceName << "\n";

  return true;
}

// ---- Logical Device Creation ----
static bool create_logical_device(RenderSystem *render) {
  QueueFamilyIndices indices =
      find_queue_families(render->physical_device, render->surface);

  // Store queue families
  render->graphics_family = indices.graphics;
  render->present_family = indices.present;

  // Create queue create infos
  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_infos[2];
  uint32_t queue_create_info_count = 0;

  // Graphics queue
  queue_create_infos[0] = {};
  queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_infos[0].queueFamilyIndex = indices.graphics;
  queue_create_infos[0].queueCount = 1;
  queue_create_infos[0].pQueuePriorities = &queue_priority;
  queue_create_info_count++;

  // Present queue (if different)
  if (indices.graphics != indices.present) {
    queue_create_infos[1] = {};
    queue_create_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[1].queueFamilyIndex = indices.present;
    queue_create_infos[1].queueCount = 1;
    queue_create_infos[1].pQueuePriorities = &queue_priority;
    queue_create_info_count++;
  }

  // Device features (empty for now)
  VkPhysicalDeviceFeatures device_features{};

  // Required extensions
  const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  // Device create info
  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.queueCreateInfoCount = queue_create_info_count;
  create_info.pQueueCreateInfos = queue_create_infos;
  create_info.pEnabledFeatures = &device_features;
  create_info.enabledExtensionCount = 1;
  create_info.ppEnabledExtensionNames = device_extensions;

#ifdef VK_ENABLE_VALIDATION
  create_info.enabledLayerCount = VALIDATION_LAYER_COUNT;
  create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#else
  create_info.enabledLayerCount = 0;
#endif

  VkResult result = vkCreateDevice(render->physical_device, &create_info,
                                   nullptr, &render->device);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create logical device: " << result << "\n";
    return false;
  }

  // Get queue handles
  vkGetDeviceQueue(render->device, indices.graphics, 0,
                   &render->graphics_queue);
  vkGetDeviceQueue(render->device, indices.present, 0, &render->present_queue);

  std::cout << "[Vulkan] Logical device created\n";
  std::cout << "[Vulkan] Graphics queue family: " << indices.graphics << "\n";
  std::cout << "[Vulkan] Present queue family: " << indices.present << "\n";

  return true;
}
// ---- Swapchain Support Query ----
struct SwapchainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  VkSurfaceFormatKHR formats[32];
  uint32_t format_count;
  VkPresentModeKHR present_modes[8];
  uint32_t present_mode_count;
};

static SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device,
                                                       VkSurfaceKHR surface) {
  SwapchainSupportDetails details{};

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
                                            &details.capabilities);

  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count,
                                       nullptr);
  if (details.format_count > 32)
    details.format_count = 32;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count,
                                       details.formats);

  vkGetPhysicalDeviceSurfacePresentModesKHR(
      device, surface, &details.present_mode_count, nullptr);
  if (details.present_mode_count > 8)
    details.present_mode_count = 8;
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      device, surface, &details.present_mode_count, details.present_modes);

  return details;
}

// ---- Choose Best Settings ----
static VkSurfaceFormatKHR
choose_surface_format(const SwapchainSupportDetails &details) {
  // Prefer BGRA8 SRGB
  for (uint32_t i = 0; i < details.format_count; ++i) {
    if (details.formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
        details.formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return details.formats[i];
    }
  }

  // Fallback to first available
  return details.formats[0];
}

static VkPresentModeKHR
choose_present_mode(const SwapchainSupportDetails &details) {
  // Prefer mailbox (triple buffering) for low latency
  for (uint32_t i = 0; i < details.present_mode_count; ++i) {
    if (details.present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      return VK_PRESENT_MODE_MAILBOX_KHR;
    }
  }

  // FIFO is always available (vsync)
  return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D
choose_swap_extent(const VkSurfaceCapabilitiesKHR &capabilities,
                   GLFWwindow *window) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  int width, height;
  glfwGetFramebufferSize(window, &width, &height);

  VkExtent2D actual_extent = {static_cast<uint32_t>(width),
                              static_cast<uint32_t>(height)};

  actual_extent.width = std::max(
      capabilities.minImageExtent.width,
      std::min(capabilities.maxImageExtent.width, actual_extent.width));
  actual_extent.height = std::max(
      capabilities.minImageExtent.height,
      std::min(capabilities.maxImageExtent.height, actual_extent.height));

  return actual_extent;
}

// ---- Create Swapchain ----
static bool create_swapchain(RenderSystem *render) {
  SwapchainSupportDetails support =
      query_swapchain_support(render->physical_device, render->surface);

  VkSurfaceFormatKHR surface_format = choose_surface_format(support);
  VkPresentModeKHR present_mode = choose_present_mode(support);
  VkExtent2D extent = choose_swap_extent(support.capabilities, render->window);

  // Request one more than minimum for triple buffering
  uint32_t image_count = support.capabilities.minImageCount + 1;
  if (support.capabilities.maxImageCount > 0 &&
      image_count > support.capabilities.maxImageCount) {
    image_count = support.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = render->surface;
  create_info.minImageCount = image_count;
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t queue_family_indices[] = {render->graphics_family,
                                     render->present_family};

  if (render->graphics_family != render->present_family) {
    create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    create_info.queueFamilyIndexCount = 2;
    create_info.pQueueFamilyIndices = queue_family_indices;
  } else {
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  create_info.preTransform = support.capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = VK_NULL_HANDLE;

  VkResult result = vkCreateSwapchainKHR(render->device, &create_info, nullptr,
                                         &render->swapchain);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create swapchain: " << result << "\n";
    return false;
  }

  // Store swapchain properties
  render->swapchain_format = surface_format.format;
  render->swapchain_extent = extent;

  // Retrieve swapchain images
  vkGetSwapchainImagesKHR(render->device, render->swapchain,
                          &render->swapchain_image_count, nullptr);
  if (render->swapchain_image_count > 8)
    render->swapchain_image_count = 8;
  vkGetSwapchainImagesKHR(render->device, render->swapchain,
                          &render->swapchain_image_count,
                          render->swapchain_images);

  std::cout << "[Vulkan] Swapchain created: " << extent.width << "x"
            << extent.height << " (" << render->swapchain_image_count
            << " images)\n";

  return true;
}

// ---- Create Image Views ----
static bool create_image_views(RenderSystem *render) {
  for (uint32_t i = 0; i < render->swapchain_image_count; ++i) {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = render->swapchain_images[i];
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = render->swapchain_format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(render->device, &create_info, nullptr,
                                        &render->swapchain_image_views[i]);
    if (result != VK_SUCCESS) {
      std::cerr << "[Vulkan] Failed to create image view " << i << "\n";
      return false;
    }
  }

  std::cout << "[Vulkan] Created " << render->swapchain_image_count
            << " image views\n";
  return true;
}
// ---- Render Pass Creation ----
static bool create_render_pass(RenderSystem *render) {
  // Single color attachment
  VkAttachmentDescription color_attachment{};
  color_attachment.format = render->swapchain_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref{};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;

  // Subpass dependency for layout transitions
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;

  VkResult result = vkCreateRenderPass(render->device, &render_pass_info,
                                       nullptr, &render->render_pass);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create render pass: " << result << "\n";
    return false;
  }

  std::cout << "[Vulkan] Render pass created\n";
  return true;
}
// ---- Framebuffer Creation ----
static bool create_framebuffers(RenderSystem *render) {
  for (uint32_t i = 0; i < render->swapchain_image_count; ++i) {
    VkImageView attachments[] = {render->swapchain_image_views[i]};

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render->render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = attachments;
    framebuffer_info.width = render->swapchain_extent.width;
    framebuffer_info.height = render->swapchain_extent.height;
    framebuffer_info.layers = 1;

    VkResult result = vkCreateFramebuffer(render->device, &framebuffer_info,
                                          nullptr, &render->framebuffers[i]);
    if (result != VK_SUCCESS) {
      std::cerr << "[Vulkan] Failed to create framebuffer " << i << "\n";
      return false;
    }
  }

  std::cout << "[Vulkan] Created " << render->swapchain_image_count
            << " framebuffers\n";
  return true;
}
// ---- Command Pool Creation ----
static bool create_command_pool(RenderSystem *render) {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = render->graphics_family;

  VkResult result = vkCreateCommandPool(render->device, &pool_info, nullptr,
                                        &render->command_pool);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create command pool: " << result << "\n";
    return false;
  }

  std::cout << "[Vulkan] Command pool created\n";
  return true;
}

// ---- Command Buffer Allocation ----
static bool create_command_buffers(RenderSystem *render) {
  VkCommandBufferAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = render->command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = render->swapchain_image_count;

  VkResult result = vkAllocateCommandBuffers(render->device, &alloc_info,
                                             render->command_buffers);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to allocate command buffers: " << result
              << "\n";
    return false;
  }

  std::cout << "[Vulkan] Allocated " << render->swapchain_image_count
            << " command buffers\n";
  return true;
}

// ---- Record Command Buffer ----
static void record_command_buffer(RenderSystem *render, uint32_t image_index) {
  VkCommandBuffer cmd = render->command_buffers[image_index];

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to begin command buffer\n";
    return;
  }

  // Begin render pass
  VkRenderPassBeginInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_info.renderPass = render->render_pass;
  render_pass_info.framebuffer = render->framebuffers[image_index];
  render_pass_info.renderArea.offset = {0, 0};
  render_pass_info.renderArea.extent = render->swapchain_extent;

  VkClearValue clear_color = {{{0.1f, 0.1f, 0.15f, 1.0f}}};
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

  // Bind pipeline
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    render->graphics_pipeline);

  // Push constants with camera
  PushConstants push{};

  // ADD THESE TWO LINES:
  float width = static_cast<float>(render->swapchain_extent.width);
  float height = static_cast<float>(render->swapchain_extent.height);

  // Create orthographic projection with camera
  float zoom = render->camera_zoom;
  float aspect = width / height;

  // ... rest of function continues

  // Simple ortho matrix (maps 0-200 world units to screen)
  float left = 0.0f;
  float right = 200.0f;
  float bottom = -400.0f;
  float top = 0.0f;

  // Column-major 4x4 matrix
  push.view_projection[0] = 2.0f / (right - left);
  push.view_projection[1] = 0.0f;
  push.view_projection[2] = 0.0f;
  push.view_projection[3] = 0.0f;

  push.view_projection[4] = 0.0f;
  push.view_projection[5] = 2.0f / (top - bottom);
  push.view_projection[6] = 0.0f;
  push.view_projection[7] = 0.0f;

  push.view_projection[8] = 0.0f;
  push.view_projection[9] = 0.0f;
  push.view_projection[10] = 1.0f;
  push.view_projection[11] = 0.0f;

  push.view_projection[12] = -(right + left) / (right - left);
  push.view_projection[13] = -(top + bottom) / (top - bottom);
  push.view_projection[14] = 0.0f;
  push.view_projection[15] = 1.0f;

  vkCmdPushConstants(cmd, render->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                     0, sizeof(PushConstants), &push);

  // Bind vertex buffer (circle geometry)
  VkBuffer vertex_buffers[] = {render->circle_vertex_buffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

  // Bind instance buffer
  VkBuffer instance_buffers[] = {render->instance_buffer};
  vkCmdBindVertexBuffers(cmd, 1, 1, instance_buffers, offsets);

  // Draw all instances
  vkCmdDraw(cmd, render->circle_vertex_count, render->instance_count, 0, 0);

  vkCmdEndRenderPass(cmd);

  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to record command buffer\n";
  }
}
// ---- Synchronization Objects ----
static bool create_sync_objects(RenderSystem *render) {
  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  // Create semaphores for each swapchain image
  for (uint32_t i = 0; i < render->swapchain_image_count; ++i) {
    if (vkCreateSemaphore(render->device, &semaphore_info, nullptr,
                          &render->image_available_semaphores[i]) !=
            VK_SUCCESS ||
        vkCreateSemaphore(render->device, &semaphore_info, nullptr,
                          &render->render_finished_semaphores[i]) !=
            VK_SUCCESS) {
      std::cerr << "[Vulkan] Failed to create semaphores\n";
      return false;
    }
  }

  // Create fences for frames in flight
  for (int i = 0; i < 2; ++i) {
    if (vkCreateFence(render->device, &fence_info, nullptr,
                      &render->in_flight_fences[i]) != VK_SUCCESS) {
      std::cerr << "[Vulkan] Failed to create fences\n";
      return false;
    }
  }

  std::cout << "[Vulkan] Sync objects created ("
            << render->swapchain_image_count << " semaphore sets, 2 fences)\n";
  return true;
}
// ---- Shader Loading ----
#include <fstream>
#include <vector>

static std::vector<char> read_file(const char *filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    std::cerr << "[Vulkan] Failed to open shader file: " << filename << "\n";
    return {};
  }

  size_t file_size = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(file_size);

  file.seekg(0);
  file.read(buffer.data(), file_size);
  file.close();

  return buffer;
}

static VkShaderModule create_shader_module(VkDevice device,
                                           const std::vector<char> &code) {
  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size();
  create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule shader_module;
  if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) !=
      VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create shader module\n";
    return VK_NULL_HANDLE;
  }

  return shader_module;
}

static bool load_shaders(RenderSystem *render) {
  // Load vertex shader
  auto vert_code = read_file("shaders/map_node.vert.spv");
  if (vert_code.empty())
    return false;

  render->vertex_shader = create_shader_module(render->device, vert_code);
  if (render->vertex_shader == VK_NULL_HANDLE)
    return false;

  // Load fragment shader
  auto frag_code = read_file("shaders/map_node.frag.spv");
  if (frag_code.empty())
    return false;

  render->fragment_shader = create_shader_module(render->device, frag_code);
  if (render->fragment_shader == VK_NULL_HANDLE)
    return false;

  std::cout << "[Vulkan] Shaders loaded\n";
  return true;
}
// ---- Pipeline Layout Creation ----
static bool create_pipeline_layout(RenderSystem *render) {
  // Push constant range (camera matrix)
  VkPushConstantRange push_constant_range{};
  push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant_range.offset = 0;
  push_constant_range.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = 0; // No descriptor sets
  layout_info.pSetLayouts = nullptr;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_constant_range;

  VkResult result = vkCreatePipelineLayout(render->device, &layout_info,
                                           nullptr, &render->pipeline_layout);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create pipeline layout: " << result
              << "\n";
    return false;
  }

  std::cout << "[Vulkan] Pipeline layout created\n";
  return true;
}
// ---- Graphics Pipeline Creation ----
static bool create_graphics_pipeline(RenderSystem *render) {
  // Shader stages
  VkPipelineShaderStageCreateInfo vert_stage{};
  vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_stage.module = render->vertex_shader;
  vert_stage.pName = "main";

  VkPipelineShaderStageCreateInfo frag_stage{};
  frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_stage.module = render->fragment_shader;
  frag_stage.pName = "main";

  VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

  // Vertex input - binding 0 (per-vertex), binding 1 (per-instance)
  VkVertexInputBindingDescription bindings[2] = {};

  // Binding 0: Vertex data (circle geometry)
  bindings[0].binding = 0;
  bindings[0].stride = sizeof(Vertex);
  bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  // Binding 1: Instance data (node info)
  bindings[1].binding = 1;
  bindings[1].stride = sizeof(InstanceData);
  bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

  // Vertex attributes
  VkVertexInputAttributeDescription attributes[4] = {};

  // Location 0: vertex position (from binding 0)
  attributes[0].binding = 0;
  attributes[0].location = 0;
  attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[0].offset = offsetof(Vertex, pos);

  // Location 1: instance position (from binding 1)
  attributes[1].binding = 1;
  attributes[1].location = 1;
  attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributes[1].offset = offsetof(InstanceData, position);

  // Location 2: instance color (from binding 1)
  attributes[2].binding = 1;
  attributes[2].location = 2;
  attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributes[2].offset = offsetof(InstanceData, color);

  // Location 3: instance scale (from binding 1)
  attributes[3].binding = 1;
  attributes[3].location = 3;
  attributes[3].format = VK_FORMAT_R32_SFLOAT;
  attributes[3].offset = offsetof(InstanceData, scale);

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 2;
  vertex_input.pVertexBindingDescriptions = bindings;
  vertex_input.vertexAttributeDescriptionCount = 4;
  vertex_input.pVertexAttributeDescriptions = attributes;

  // Input assembly
  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  // Viewport and scissor (dynamic, will set in command buffer)
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(render->swapchain_extent.width);
  viewport.height = static_cast<float>(render->swapchain_extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = render->swapchain_extent;

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  // Rasterization
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  // Multisampling (disabled)
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Color blending (alpha blending)
  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo color_blending{};
  color_blending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;

  // Create pipeline
  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pDepthStencilState = nullptr;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = nullptr;
  pipeline_info.layout = render->pipeline_layout;
  pipeline_info.renderPass = render->render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

  VkResult result = vkCreateGraphicsPipelines(render->device, VK_NULL_HANDLE, 1,
                                              &pipeline_info, nullptr,
                                              &render->graphics_pipeline);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create graphics pipeline: " << result
              << "\n";
    return false;
  }

  std::cout << "[Vulkan] Graphics pipeline created\n";
  return true;
}
// ---- VMA Initialization ----
static bool init_vma(RenderSystem *render) {
  VmaVulkanFunctions vk_funcs{};
  vk_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vk_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocator_info{};
  allocator_info.vulkanApiVersion = VK_API_VERSION_1_0;
  allocator_info.physicalDevice = render->physical_device;
  allocator_info.device = render->device;
  allocator_info.instance = render->instance;
  allocator_info.pVulkanFunctions = &vk_funcs;

  VkResult result = vmaCreateAllocator(&allocator_info, &render->allocator);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create VMA allocator: " << result << "\n";
    return false;
  }

  std::cout << "[Vulkan] VMA allocator created\n";
  return true;
}

// ---- Circle Geometry Generation ----
#include <cmath>

static bool create_circle_geometry(RenderSystem *render) {
  constexpr int segments = 32;
  constexpr float radius = 1.0f; // Unit circle, scaled per-instance

  render->circle_vertex_count =
      segments + 2; // Center + perimeter + closing vertex

  std::vector<Vertex> vertices;
  vertices.reserve(render->circle_vertex_count);

  // Center vertex
  vertices.push_back({{0.0f, 0.0f}});

  // Perimeter vertices
  for (int i = 0; i <= segments; ++i) {
    float angle = (i * 2.0f * M_PI) / segments;
    vertices.push_back({{radius * cosf(angle), radius * sinf(angle)}});
  }

  // Create buffer
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = sizeof(Vertex) * vertices.size();
  buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
  alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VkResult result = vmaCreateBuffer(render->allocator, &buffer_info,
                                    &alloc_info, &render->circle_vertex_buffer,
                                    &render->circle_vertex_allocation, nullptr);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create circle vertex buffer: " << result
              << "\n";
    return false;
  }

  // Copy data to GPU
  void *data;
  vmaMapMemory(render->allocator, render->circle_vertex_allocation, &data);
  memcpy(data, vertices.data(), sizeof(Vertex) * vertices.size());
  vmaUnmapMemory(render->allocator, render->circle_vertex_allocation);

  std::cout << "[Vulkan] Circle geometry created ("
            << render->circle_vertex_count << " vertices)\n";
  return true;
}
// ---- Instance Buffer Creation ----
static bool create_instance_buffer(RenderSystem *render,
                                   const MapGenerator *map_gen) {
  auto map_data = map_gen->get_map_data();

  // Count connected nodes (nodes with outgoing connections)
  uint32_t connected_count = 0;
  for (const auto &node : *map_data.nodes) {
    if (node.next_count > 0 || node.row == map_data.height - 1) {
      connected_count++;
    }
  }

  render->instance_count = connected_count;

  if (render->instance_count == 0) {
    std::cerr << "[Vulkan] No nodes to render\n";
    return false;
  }

  // Create buffer
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = sizeof(InstanceData) * render->instance_count;
  buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
  alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VkResult result = vmaCreateBuffer(render->allocator, &buffer_info,
                                    &alloc_info, &render->instance_buffer,
                                    &render->instance_allocation, nullptr);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create instance buffer: " << result
              << "\n";
    return false;
  }

  // Map node data to instance data
  void *data;
  vmaMapMemory(render->allocator, render->instance_allocation, &data);
  InstanceData *instances = static_cast<InstanceData *>(data);

  uint32_t instance_idx = 0;
  for (const auto &node : *map_data.nodes) {
    if (node.next_count == 0 && node.row != map_data.height - 1)
      continue;

    // Position
    instances[instance_idx].position[0] = node.position.x;
    instances[instance_idx].position[1] = node.position.y;

    // Color based on type
    switch (node.type) {
    case MapNodeData::Type::Enemy:
      instances[instance_idx].color[0] = 1.0f; // Red
      instances[instance_idx].color[1] = 0.3f;
      instances[instance_idx].color[2] = 0.3f;
      instances[instance_idx].color[3] = 1.0f;
      break;
    case MapNodeData::Type::Shelter:
      instances[instance_idx].color[0] = 0.3f; // Blue
      instances[instance_idx].color[1] = 0.6f;
      instances[instance_idx].color[2] = 1.0f;
      instances[instance_idx].color[3] = 1.0f;
      break;
    case MapNodeData::Type::Wenny:
      instances[instance_idx].color[0] = 1.0f; // Yellow
      instances[instance_idx].color[1] = 0.9f;
      instances[instance_idx].color[2] = 0.2f;
      instances[instance_idx].color[3] = 1.0f;
      break;
    case MapNodeData::Type::Boss:
      instances[instance_idx].color[0] = 0.8f; // Purple
      instances[instance_idx].color[1] = 0.2f;
      instances[instance_idx].color[2] = 0.8f;
      instances[instance_idx].color[3] = 1.0f;
      break;
    default:
      instances[instance_idx].color[0] = 0.5f; // Gray
      instances[instance_idx].color[1] = 0.5f;
      instances[instance_idx].color[2] = 0.5f;
      instances[instance_idx].color[3] = 1.0f;
      break;
    }

    // Scale
    instances[instance_idx].scale = 8.0f; // Node radius

    instance_idx++;
  }

  vmaUnmapMemory(render->allocator, render->instance_allocation);

  std::cout << "[Vulkan] Instance buffer created (" << render->instance_count
            << " nodes)\n";
  return true;
}
// ---- Public API ----
void render_init(RenderSystem *render, GLFWwindow *window,
                 const MapGenerator *map_gen) {
  render->window = window;

  if (!create_instance(render))
    return;

#ifdef VK_ENABLE_VALIDATION
  if (create_debug_messenger(render->instance, &render->debug_messenger) !=
      VK_SUCCESS) {
    std::cerr << "[Vulkan] Debug messenger creation failed\n";
  } else {
    std::cout << "[Vulkan] Debug messenger created\n";
  }
#endif

  VkResult result = glfwCreateWindowSurface(render->instance, window, nullptr,
                                            &render->surface);
  if (result != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to create surface: " << result << "\n";
    return;
  }
  std::cout << "[Vulkan] Surface created\n";

  if (!pick_physical_device(render))
    return;
  if (!create_logical_device(render))
    return;
  // create swapchain
  if (!create_swapchain(render))
    return;
  if (!create_image_views(render))
    return;
  if (!create_render_pass(render))
    return;
  if (!create_framebuffers(render))
    return;
  if (!create_command_pool(render))
    return;
  if (!create_command_buffers(render))
    return;
  if (!create_sync_objects(render))
    return;
  if (!load_shaders(render))
    return;
  if (!create_pipeline_layout(render))
    return;
  if (!create_graphics_pipeline(render))
    return;
  if (!init_vma(render))
    return;
  if (!create_circle_geometry(render))
    return;
  if (!create_instance_buffer(render, map_gen))
    return;
  std::cout << "[Vulkan] Render system initialized\n";
}

void render_cleanup(RenderSystem *render) {
  if (render->device) {
    vkDeviceWaitIdle(render->device); // Wait before cleanup
  }
  // Destroy buffers:
  if (render->instance_buffer) {
    vmaDestroyBuffer(render->allocator, render->instance_buffer,
                     render->instance_allocation);
  }
  if (render->circle_vertex_buffer) {
    vmaDestroyBuffer(render->allocator, render->circle_vertex_buffer,
                     render->circle_vertex_allocation);
  }

  // Destroy VMA allocator
  if (render->allocator) {
    vmaDestroyAllocator(render->allocator);
    std::cout << "[Vulkan] VMA allocator destroyed\n";
  }
  // Destroy command pool (automatically frees command buffers)
  if (render->command_pool) {
    vkDestroyCommandPool(render->device, render->command_pool, nullptr);
    std::cout << "[Vulkan] Command pool destroyed\n";
  }
  // Destroy sync objects
  for (uint32_t i = 0; i < render->swapchain_image_count; ++i) { // Semaphores
    if (render->image_available_semaphores[i]) {
      vkDestroySemaphore(render->device, render->image_available_semaphores[i],
                         nullptr);
    }
    if (render->render_finished_semaphores[i]) {
      vkDestroySemaphore(render->device, render->render_finished_semaphores[i],
                         nullptr);
    }
  }

  for (int i = 0; i < 2; ++i) { // Fences
    if (render->in_flight_fences[i]) {
      vkDestroyFence(render->device, render->in_flight_fences[i], nullptr);
    }
  }
  // Destroy shader modules
  if (render->vertex_shader) {
    vkDestroyShaderModule(render->device, render->vertex_shader, nullptr);
  }
  if (render->fragment_shader) {
    vkDestroyShaderModule(render->device, render->fragment_shader, nullptr);
  }
  // Destroy pipeline layout
  if (render->pipeline_layout) {
    vkDestroyPipelineLayout(render->device, render->pipeline_layout, nullptr);
  }
  // Destroy graphics pipeline
  if (render->graphics_pipeline) {
    vkDestroyPipeline(render->device, render->graphics_pipeline, nullptr);
  }
  // Destroy framebuffers
  for (uint32_t i = 0; i < render->swapchain_image_count; ++i) {
    if (render->framebuffers[i]) {
      vkDestroyFramebuffer(render->device, render->framebuffers[i], nullptr);
    }
  }

  // Destroy render pass
  if (render->render_pass) {
    vkDestroyRenderPass(render->device, render->render_pass, nullptr);
    std::cout << "[Vulkan] Render pass destroyed\n";
  }

  // Destroy image views
  for (uint32_t i = 0; i < render->swapchain_image_count; ++i) {
    if (render->swapchain_image_views[i]) {
      vkDestroyImageView(render->device, render->swapchain_image_views[i],
                         nullptr);
    }
  }

  // Destroy swapchain
  if (render->swapchain) {
    vkDestroySwapchainKHR(render->device, render->swapchain, nullptr);
    std::cout << "[Vulkan] Swapchain destroyed\n";
  }

  if (render->device) {
    vkDestroyDevice(render->device, nullptr);
    std::cout << "[Vulkan] Device destroyed\n";
  }
  if (render->device) {
    vkDestroyDevice(render->device, nullptr);
    std::cout << "[Vulkan] Device destroyed\n";
  }

#ifdef VK_ENABLE_VALIDATION

  if (render->debug_messenger) {
    destroy_debug_messenger(render->instance, render->debug_messenger);
  }
#endif

  if (render->surface) {
    vkDestroySurfaceKHR(render->instance, render->surface, nullptr);
    std::cout << "[Vulkan] Surface destroyed\n";
  }

  if (render->instance) {
    vkDestroyInstance(render->instance, nullptr);
    std::cout << "[Vulkan] Instance destroyed\n";
  }
}

void render_frame(RenderSystem *render, const UISystem *ui,
                  const MapGenerator *map_gen) {
  (void)ui;
  (void)map_gen;

  uint32_t frame = render->current_frame;

  // Wait for this frame's fence
  vkWaitForFences(render->device, 1, &render->in_flight_fences[frame], VK_TRUE,
                  UINT64_MAX);

  // Acquire image (use FRAME semaphore for acquire, IMAGE semaphore for signal)
  uint32_t image_index;
  VkResult result = vkAcquireNextImageKHR(
      render->device, render->swapchain, UINT64_MAX,
      render->image_available_semaphores[frame], // Still use frame for acquire
      VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    std::cerr << "[Vulkan] Failed to acquire swapchain image\n";
    return;
  }

  // Check if this image is in use by a previous frame
  if (render->images_in_flight[image_index] != VK_NULL_HANDLE) {
    vkWaitForFences(render->device, 1, &render->images_in_flight[image_index],
                    VK_TRUE, UINT64_MAX);
  }
  render->images_in_flight[image_index] = render->in_flight_fences[frame];
  // Reset fence
  vkResetFences(render->device, 1, &render->in_flight_fences[frame]);
  // Re-record command buffer with current camera
  record_command_buffer(render, image_index);
  // Submit - use IMAGE INDEX for both wait and signal semaphores
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore wait_semaphores[] = {render->image_available_semaphores[frame]};
  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &render->command_buffers[image_index];

  VkSemaphore signal_semaphores[] = {
      render->render_finished_semaphores[image_index]};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_semaphores;

  if (vkQueueSubmit(render->graphics_queue, 1, &submit_info,
                    render->in_flight_fences[frame]) != VK_SUCCESS) {
    std::cerr << "[Vulkan] Failed to submit\n";
    return;
  }

  // Present - wait on IMAGE semaphore
  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores; // Same as above

  VkSwapchainKHR swapchains[] = {render->swapchain};
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapchains;
  present_info.pImageIndices = &image_index;

  vkQueuePresentKHR(render->present_queue, &present_info);

  render->current_frame = (render->current_frame + 1) % 2;
}
