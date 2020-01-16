//
//  hello-triangle.c
//
//  Created by John Watson on 1/14/20.
//

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.h>

typedef struct queue_family_indices {
    uint32_t graphicsFamily;
    bool didSetGraphicsFamily;

    uint32_t presentFamily;
    bool didSetPresentFamily;
} queue_family_indices_t;

typedef struct swapchain_support_details {
    VkSurfaceCapabilitiesKHR capabilities;

    VkSurfaceFormatKHR *formats;
    uint32_t formatCount;

    VkPresentModeKHR* presentModes;
    uint32_t presentModeCount;
} swapchain_support_details_t;

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

const char *APP_NAME = "Test App";

const int WIDTH = 800;
const int HEIGHT = 600;

VkInstance vulkanInstance = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice logicalDevice = VK_NULL_HANDLE;
VkQueue graphicsQueue = VK_NULL_HANDLE;
VkQueue presentQueue = VK_NULL_HANDLE;
VkSurfaceKHR vulkanSurface = VK_NULL_HANDLE;
queue_family_indices_t queueFamilyIndices;
VkSwapchainKHR swapChain;
VkImage *swapchainImages;
uint32_t swapchainImageCount = 0;
VkFormat swapchainImageFormat;
VkExtent2D swapchainExtent;
VkImageView *swapChainImageViews;
VkRenderPass renderPass;
VkPipelineLayout pipelineLayout;
VkPipeline graphicsPipeline;
VkFramebuffer *swapchainFramebuffers;
VkCommandPool commandPool;
VkCommandBuffer *commandBuffers;
VkSemaphore imageAvailableSemaphore;
VkSemaphore renderFinishedSemaphore;

const char *requiredExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

const uint32_t requiredExtensionCount = 1;

char* GetResourcePath(char *filename) {
    static char *basePath;
    if (!basePath) {
        basePath = SDL_GetBasePath();
    }
    size_t len = strlen(basePath) + strlen(filename);
    char *path = malloc(len + 1);
    sprintf(path, "%s%s", basePath, filename);
    return path;
}

char* ReadBytesFromResource(char *name, long *sizeOut) {
    char *path = GetResourcePath(name);
    FILE *f = fopen(path, "rb");
    free(path);

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (sizeOut) {
        *sizeOut = size;
    }
    rewind(f);

    char *buffer = malloc(size);

    fread(buffer, 1, size, f);

    fclose(f);

    return buffer;
}

#ifndef NDEBUG
#define FatalError(...) _FatalError(__FILE__, __LINE__, __VA_ARGS__)
#else
#define FatalError(...) _FatalError(NULL, -1, __VA_ARGS__);
#endif

void _FatalError(const char *file, int line, char *fmt, ...) {
    fprintf(stderr, "FATAL ERROR");
    if (line != -1) {
        fprintf(stderr, " (%s:%d): ", file, line);
    } else {
        fprintf(stderr, ": ");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}

void InitVulkanInstance(SDL_Window *window) {
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = APP_NAME,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    uint32_t instanceExtensionCount;
    SDL_Vulkan_GetInstanceExtensions(window, &instanceExtensionCount, NULL);

    const char **extensionNames = calloc(instanceExtensionCount, sizeof(char*));
    SDL_Vulkan_GetInstanceExtensions(window, &instanceExtensionCount, extensionNames);

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = instanceExtensionCount,
        .ppEnabledExtensionNames = extensionNames,
        .enabledLayerCount = 0,
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &vulkanInstance);

    free(extensionNames);

    if (result != VK_SUCCESS) {
        FatalError("Failed to create instance");
    }
}

bool IsDiscreteGPU(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
}

void PickPhysicalVulkanDevice(void) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vulkanInstance, &deviceCount, NULL);

    if (deviceCount == 0) {
        FatalError("Failed to find Vulkan-capable GPU");
    }

    VkPhysicalDevice *devices = calloc(deviceCount, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vulkanInstance, &deviceCount, devices);

    // Prefer discrete GPU, but accept integrated GPU.
    for (size_t i = 0; i < deviceCount; i++) {
        if (IsDiscreteGPU(devices[i])) {
            physicalDevice = devices[i];
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        printf("Discrete GPU not found, falling back to integrated GPU...\n");
        physicalDevice = devices[0];
    }

    free(devices);

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, NULL);

    if (extensionCount == 0) {
        FatalError("Returned zero available extensions for device.");
    }

    VkExtensionProperties *properties = calloc(extensionCount, sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, properties);

    for (uint32_t i = 0; i < requiredExtensionCount; i++) {
        bool found = false;
        for (uint32_t j = 0; j < extensionCount; j++) {
            if (strcmp(requiredExtensions[i], properties[j].extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            FatalError("Could not find required extension %s.", requiredExtensions[i]);
        }
    }

    free(properties);
}

queue_family_indices_t FindQueueFamilies(void) {
    queue_family_indices_t indices;
    memset(&indices, 0, sizeof(queue_family_indices_t));

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);

    if (queueFamilyCount == 0) {
        return indices;
    }

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        indices.graphicsFamily = i;
        indices.didSetGraphicsFamily = true;

        VkBool32 surfaceSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, vulkanSurface, &surfaceSupported);
        if (surfaceSupported) {
            indices.presentFamily = i;
            indices.didSetPresentFamily = true;
        }
    }

    return indices;
}

void CreateLogicalDevice(void) {
    queueFamilyIndices = FindQueueFamilies();
    if (!queueFamilyIndices.didSetGraphicsFamily) {
        FatalError("Failed to find graphics queue family.");
    }

    if (!queueFamilyIndices.didSetPresentFamily) {
        FatalError("Failed to find present queue family.");
    }

    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamilyIndices.graphicsFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        },
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamilyIndices.presentFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        },
    };

    // Use separate queues if graphics and present queue families are different.
    const uint32_t queueCreateInfoCount = queueFamilyIndices.graphicsFamily == queueFamilyIndices.presentFamily ? 1 : 2;

    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queueCreateInfos,
        .queueCreateInfoCount = queueCreateInfoCount,
        .pEnabledFeatures = &features,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = requiredExtensions,
        .enabledLayerCount = 0,
    };

    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &logicalDevice) != VK_SUCCESS) {
        FatalError("Failed to create logical device.");
    }

    vkGetDeviceQueue(logicalDevice, queueFamilyIndices.graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(logicalDevice, queueFamilyIndices.presentFamily, 0, &presentQueue);
}

void CreateVulkanSurface(SDL_Window *window) {
    if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, &vulkanSurface)) {
        FatalError("Failed to create Vulkan surface: %s", SDL_GetError());
    }
}

swapchain_support_details_t QuerySwapchainSupport(void) {
    swapchain_support_details_t details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, vulkanSurface, &details.capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vulkanSurface, &details.formatCount, NULL);
    if (details.formatCount > 0) {
        details.formats = calloc(details.formatCount, sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vulkanSurface, &details.formatCount, details.formats);
    } else {
        details.formats = NULL;
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vulkanSurface, &details.presentModeCount, NULL);
    if (details.presentModeCount > 0) {
        details.presentModes = calloc(details.presentModeCount, sizeof(VkPresentModeKHR));
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vulkanSurface, &details.presentModeCount, details.presentModes);
    } else {
        details.presentModes = NULL;
    }

    return details;
}

void FreeSwapchainSupportDetails(swapchain_support_details_t details) {
    free(details.formats);
    free(details.presentModes);
}

VkSurfaceFormatKHR ChooseSwapSurfaceFormat(swapchain_support_details_t details) {
    for (uint32_t i = 0; i < details.formatCount; i++) {
        if (details.formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && details.formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return details.formats[i];
        }
    }

    return details.formats[0];
}

VkPresentModeKHR ChoosePresentMode(swapchain_support_details_t details) {
    // Prefer VK_PRESENT_MODE_MAILBOX_KHR (triple buffering) but fall back to FIFO if not available.
    for (uint32_t i = 0; i < details.presentModeCount; i++) {
        if (details.presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return details.presentModes[i];
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseSwapExtent(swapchain_support_details_t details) {
    if (details.capabilities.currentExtent.width != UINT32_MAX) {
        return details.capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {
        .width = WIDTH,
        .height = HEIGHT,
    };

    actualExtent.width = MAX(details.capabilities.minImageExtent.width, MIN(details.capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = MAX(details.capabilities.minImageExtent.height, MIN(details.capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
}

void CreateSwapChain(swapchain_support_details_t details) {
    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details);
    VkPresentModeKHR presentMode = ChoosePresentMode(details);
    VkExtent2D extent = ChooseSwapExtent(details);

    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
        imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vulkanSurface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = details.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily) {
        uint32_t indices[] = { queueFamilyIndices.graphicsFamily, queueFamilyIndices.presentFamily };
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = indices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = NULL;
    }

    if (vkCreateSwapchainKHR(logicalDevice, &createInfo, NULL, &swapChain) != VK_SUCCESS) {
        FatalError("Failed to create swapchain.");
    }

    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &swapchainImageCount, NULL);
    swapchainImages = calloc(swapchainImageCount, sizeof(VkImage));
    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &swapchainImageCount, swapchainImages);

    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = extent;
}

void CreateImageViews(void) {
    swapChainImageViews = calloc(swapchainImageCount, sizeof(VkImageView));
    for (size_t i = 0; i < swapchainImageCount; i++) {
        VkImageViewCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainImageFormat,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(logicalDevice, &createInfo, NULL, &swapChainImageViews[i]) != VK_SUCCESS) {
            FatalError("Failed to create image views.");
        }
    }
}

VkShaderModule CreateShaderModule(const char *code, long size) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = (const uint32_t *)code,
    };

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(logicalDevice, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
        FatalError("Failed to create shader module.");
    }

    return shaderModule;
}

void CreateRenderPass(void) {
    VkAttachmentDescription colorAttachment = {
        .format = swapchainImageFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference colorAttachmentRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    if (vkCreateRenderPass(logicalDevice, &renderPassInfo, NULL, &renderPass) != VK_SUCCESS) {
        FatalError("Failed to create render pass.");
    }
}

void CreateGraphicsPipeline(void) {
    long vertexShaderCodeSize, fragmentShaderCodeSize;
    char *vertexShaderCode = ReadBytesFromResource("vertex.spv", &vertexShaderCodeSize);
    char *fragmentShaderCode = ReadBytesFromResource("fragment.spv", &fragmentShaderCodeSize);

    VkShaderModule vertexShaderModule = CreateShaderModule(vertexShaderCode, vertexShaderCodeSize);
    VkShaderModule fragmentShaderModule = CreateShaderModule(fragmentShaderCode, fragmentShaderCodeSize);

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertexShaderModule,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo fragmentShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragmentShaderModule,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertexShaderStageInfo, fragmentShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = NULL,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = NULL,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewport = {
        .x = 0,
        .y = 0,
        .width = (float)swapchainExtent.width,
        .height = (float)swapchainExtent.height,
        .minDepth = 0,
        .maxDepth = 1,
    };

    VkRect2D scissor = {
        .offset = { .x = 0, .y = 0 },
        .extent = swapchainExtent,
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0,
        .depthBiasClamp = 0,
        .depthBiasSlopeFactor = 0,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .minSampleShading = 1,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = { 0, 0, 0, 0 },
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = NULL,
    };

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, NULL, &pipelineLayout) != VK_SUCCESS) {
        FatalError("Failed to create pipeline layout.");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = NULL,
        .pColorBlendState = &colorBlending,
        .pRasterizationState = &rasterizer,
        .pDynamicState = NULL,
        .layout = pipelineLayout,
        .renderPass = renderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline) != VK_SUCCESS) {
        FatalError("Failed to create graphics pipeline.");
    }

    vkDestroyShaderModule(logicalDevice, fragmentShaderModule, NULL);
    vkDestroyShaderModule(logicalDevice, vertexShaderModule, NULL);
}

void CreateFramebuffers(void) {
    swapchainFramebuffers = calloc(swapchainImageCount, sizeof(VkFramebuffer));

    for (size_t i = 0; i < swapchainImageCount; i++) {
        VkImageView attachments[] = {
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = swapchainExtent.width,
            .height = swapchainExtent.height,
            .layers = 1,
        };

        if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, NULL, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            FatalError("Failed to create framebuffer.");
        }
    }
}

void CreateCommandPool(void) {
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queueFamilyIndices.graphicsFamily,
        .flags = 0,
    };

    if (vkCreateCommandPool(logicalDevice, &poolInfo, NULL, &commandPool) != VK_SUCCESS) {
        FatalError("Failed to create command pool.");
    }
}

void CreateCommandBuffers(void) {
    commandBuffers = calloc(swapchainImageCount, sizeof(VkCommandBuffer));

    VkCommandBufferAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = swapchainImageCount,
    };

    if (vkAllocateCommandBuffers(logicalDevice, &allocateInfo, commandBuffers) != VK_SUCCESS) {
        FatalError("Failed to allocate command buffers.");
    }

    for (size_t i = 0; i < swapchainImageCount; i++) {
        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = 0,
            .pInheritanceInfo = NULL,
        };

        if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
            FatalError("Failed to begin recording command buffer.");
        }

        VkClearValue clearColor = { 0.3f, 0.3f, 0.3f, 1.0f }; // Light grey background.

        VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = renderPass,
            .framebuffer = swapchainFramebuffers[i],
            .renderArea = {
                .offset = { .x = 0, .y = 0 },
                .extent = swapchainExtent,
            },
            .clearValueCount = 1,
            .pClearValues = &clearColor,
        };

        vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffers[i]);

        if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
            FatalError("Failed to record command buffer.");
        }
    }
}

void CreateSemaphores(void) {
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, NULL, &imageAvailableSemaphore) != VK_SUCCESS) {
        FatalError("Failed to create image available semaphore.");
    }

    if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, NULL, &renderFinishedSemaphore) != VK_SUCCESS) {
        FatalError("Failed to create render finished semaphore.");
    }
}

void DrawFrame(void) {
    uint32_t imageIndex;
    vkAcquireNextImageKHR(logicalDevice, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[imageIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores,
    };

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        FatalError("Failed to submit draw command buffer.");
    }

    VkSwapchainKHR swapchains[] = { swapChain };

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = swapchains,
        .pImageIndices = &imageIndex,
    };

    vkQueuePresentKHR(presentQueue, &presentInfo);
}

int main(int argc, const char * argv[]) {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window *window = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI);

    if (!window) {
        FatalError("Failed to create window: %s", SDL_GetError());
    }

    InitVulkanInstance(window);
    CreateVulkanSurface(window);
    PickPhysicalVulkanDevice();
    CreateLogicalDevice();

    swapchain_support_details_t swapchainDetails = QuerySwapchainSupport();
    if (swapchainDetails.formatCount == 0 || swapchainDetails.presentModeCount == 0) {
        FatalError("No valid swapchain configuration found.");
    }

    CreateSwapChain(swapchainDetails);

    FreeSwapchainSupportDetails(swapchainDetails);

    CreateImageViews();
    CreateRenderPass();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSemaphores();

    DrawFrame();

    SDL_Event event;

    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_QUIT) {
            break;
        }
    }

    vkDestroySemaphore(logicalDevice, renderFinishedSemaphore, NULL);
    vkDestroySemaphore(logicalDevice, imageAvailableSemaphore, NULL);

    vkDestroyCommandPool(logicalDevice, commandPool, NULL);

    for (size_t i = 0; i < swapchainImageCount; i++) {
        vkDestroyFramebuffer(logicalDevice, swapchainFramebuffers[i], NULL);
    }

    vkDestroyPipeline(logicalDevice, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(logicalDevice, pipelineLayout, NULL);
    vkDestroyRenderPass(logicalDevice, renderPass, NULL);

    for (size_t i = 0; i < swapchainImageCount; i++) {
        vkDestroyImageView(logicalDevice, swapChainImageViews[i], NULL);
    }

    vkDestroySwapchainKHR(logicalDevice, swapChain, NULL);
    vkDestroySurfaceKHR(vulkanInstance, vulkanSurface, NULL);
    vkDestroyDevice(logicalDevice, NULL);
    vkDestroyInstance(vulkanInstance, NULL);

    SDL_Quit();

    return 0;
}
