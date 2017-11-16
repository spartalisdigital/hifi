//
//  LightStage.cpp
//  render-utils/src
//
//  Created by Zach Pomerantz on 1/14/2015.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ViewFrustum.h"

#include "LightStage.h"

#include <cmath>

std::string LightStage::_stageName { "LIGHT_STAGE"};
const glm::mat4 LightStage::Shadow::_biasMatrix{
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 0.5, 0.0,
    0.5, 0.5, 0.5, 1.0 };
const int LightStage::Shadow::MAP_SIZE = 1024;

const unsigned int LightStage::SUN_SHADOW_CASCADE_COUNT{ 3 };
const LightStage::Index LightStage::INVALID_INDEX { render::indexed_container::INVALID_INDEX };

LightStage::LightStage() {
}

LightStage::Shadow::Schema::Schema() {
    ShadowTransform defaultTransform;
    defaultTransform.bias = 0.005f;
    std::fill(cascades, cascades + SHADOW_CASCADE_MAX_COUNT, defaultTransform);
    invMapSize = 1.0f / MAP_SIZE;
    cascadeCount = 1;
}

LightStage::Shadow::Cascade::Cascade() : _frustum{ std::make_shared<ViewFrustum>() } {
    framebuffer = gpu::FramebufferPointer(gpu::Framebuffer::createShadowmap(MAP_SIZE));
    map = framebuffer->getDepthStencilBuffer();
}

const glm::mat4& LightStage::Shadow::Cascade::getView() const {
    return _frustum->getView();
}

const glm::mat4& LightStage::Shadow::Cascade::getProjection() const {
    return _frustum->getProjection();
}

LightStage::Shadow::Shadow(model::LightPointer light, unsigned int cascadeCount) : _light{ light } {
    cascadeCount = std::min(cascadeCount, (unsigned int)SHADOW_CASCADE_MAX_COUNT);

    Schema schema;
    _schemaBuffer = std::make_shared<gpu::Buffer>(sizeof(Schema), (const gpu::Byte*) &schema);
    _cascades.resize(cascadeCount);
    _schemaBuffer.edit<Schema>().cascadeCount = cascadeCount;
}

void LightStage::Shadow::setKeylightFrustum(unsigned int cascadeIndex, const ViewFrustum& viewFrustum,
                                            float viewMinShadowDistance, float viewMaxShadowDistance, float viewOverlapDistance,
                                            float nearDepth, float farDepth) {
    assert(viewMinShadowDistance < viewMaxShadowDistance);
    assert(nearDepth < farDepth);
    assert(viewOverlapDistance > 0.0f);
    assert(cascadeIndex < _cascades.size());

    // Orient the keylight frustum
    const auto& direction = glm::normalize(_light->getDirection());
    glm::quat orientation;
    if (direction == IDENTITY_UP) {
        orientation = glm::quat(glm::mat3(-IDENTITY_RIGHT, IDENTITY_FORWARD, -IDENTITY_UP));
    } else if (direction == -IDENTITY_UP) {
        orientation = glm::quat(glm::mat3(IDENTITY_RIGHT, IDENTITY_FORWARD, IDENTITY_UP));
    } else {
        auto side = glm::normalize(glm::cross(direction, IDENTITY_UP));
        auto up = glm::normalize(glm::cross(side, direction));
        orientation = glm::quat_cast(glm::mat3(side, up, -direction));
    }

    auto& cascade = _cascades[cascadeIndex];

    cascade._frustum->setOrientation(orientation);

    // Position the keylight frustum
    cascade._frustum->setPosition(viewFrustum.getPosition() - (nearDepth + farDepth)*direction);

    const Transform view{ cascade._frustum->getView()};
    const Transform viewInverse{ view.getInverseMatrix() };

    auto nearCorners = viewFrustum.getCorners(viewMinShadowDistance);
    auto farCorners = viewFrustum.getCorners(viewMaxShadowDistance);

    vec3 min{ viewInverse.transform(nearCorners.bottomLeft) };
    vec3 max{ min };
    // Expand keylight frustum  to fit view frustum
    auto fitFrustum = [&min, &max, &viewInverse](const vec3& viewCorner) {
        const auto corner = viewInverse.transform(viewCorner);

        min.x = glm::min(min.x, corner.x);
        min.y = glm::min(min.y, corner.y);
        min.z = glm::min(min.z, corner.z);

        max.x = glm::max(max.x, corner.x);
        max.y = glm::max(max.y, corner.y);
        max.z = glm::max(max.z, corner.z);
    };
    fitFrustum(nearCorners.bottomRight);
    fitFrustum(nearCorners.topLeft);
    fitFrustum(nearCorners.topRight);
    fitFrustum(farCorners.bottomLeft);
    fitFrustum(farCorners.bottomRight);
    fitFrustum(farCorners.topLeft);
    fitFrustum(farCorners.topRight);

    // Re-adjust near shadow distance
    auto near = glm::max(max.z, -nearDepth);
    auto far = -min.z;
    glm::mat4 ortho = glm::ortho<float>(min.x, max.x, min.y, max.y, near, far);
    cascade._frustum->setProjection(ortho);

    // Calculate the frustum's internal state
    cascade._frustum->calculate();

    // Update the buffer
    auto& schemaCascade = _schemaBuffer.edit<Schema>().cascades[cascadeIndex];
    schemaCascade.reprojection = _biasMatrix * ortho * viewInverse.getMatrix();
    schemaCascade.invTransitionWidth = 1.0f / viewOverlapDistance;
    schemaCascade.maxDistance = viewMaxShadowDistance;
    cascade.minDistance = viewMinShadowDistance;
    cascade.maxDistance = viewMaxShadowDistance;
}

void LightStage::Shadow::setFrustum(unsigned int cascadeIndex, const ViewFrustum& shadowFrustum) {
    assert(cascadeIndex < _cascades.size());
    const Transform view{ shadowFrustum.getView() };
    const Transform viewInverse{ view.getInverseMatrix() };
    auto& cascade = _cascades[cascadeIndex];

    *cascade._frustum = shadowFrustum;
    // Update the buffer
    _schemaBuffer.edit<Schema>().cascades[cascadeIndex].reprojection = _biasMatrix * shadowFrustum.getProjection() * viewInverse.getMatrix();
}

LightStage::Index LightStage::findLight(const LightPointer& light) const {
    auto found = _lightMap.find(light);
    if (found != _lightMap.end()) {
        return INVALID_INDEX;
    } else {
        return (*found).second;
    }
}

LightStage::Index LightStage::addLight(const LightPointer& light) {
    auto found = _lightMap.find(light);
    if (found == _lightMap.end()) {
        auto lightId = _lights.newElement(light);
        // Avoid failing to allocate a light, just pass
        if (lightId != INVALID_INDEX) {

            // Allocate the matching Desc to the light
            if (lightId >= (Index) _descs.size()) {
                _descs.emplace_back(Desc());
            } else {
                assert(_descs[lightId].shadowId == INVALID_INDEX);
                _descs[lightId] = Desc();
            }

            // INsert the light and its index in the reverese map
            _lightMap.insert(LightMap::value_type(light, lightId));

            updateLightArrayBuffer(lightId);
        }
        return lightId;
    } else {
        return (*found).second;
    }
}

LightStage::Index LightStage::addShadow(Index lightIndex, unsigned int cascadeCount) {
    auto light = getLight(lightIndex);
    Index shadowId = INVALID_INDEX;
    if (light) {
        assert(_descs[lightIndex].shadowId == INVALID_INDEX);
        shadowId = _shadows.newElement(std::make_shared<Shadow>(light, cascadeCount));
        _descs[lightIndex].shadowId = shadowId;
    }
    return shadowId;
}

LightStage::LightPointer LightStage::removeLight(Index index) {
    LightPointer removedLight = _lights.freeElement(index);
    if (removedLight) {
        auto shadowId = _descs[index].shadowId;
        // Remove shadow if one exists for this light
        if (shadowId != INVALID_INDEX) {
            auto removedShadow = _shadows.freeElement(shadowId);
            assert(removedShadow);
            assert(removedShadow->getLight() == removedLight);
        }
        _lightMap.erase(removedLight);
        _descs[index] = Desc();
    }
    assert(_descs.size() <= index || _descs[index].shadowId == INVALID_INDEX);
    return removedLight;
}

LightStage::LightPointer LightStage::getCurrentKeyLight() const {
    Index keyLightId{ 0 };
    if (!_currentFrame._sunLights.empty()) {
        keyLightId = _currentFrame._sunLights.front();
    }
    return _lights.get(keyLightId);
}

LightStage::LightPointer LightStage::getCurrentAmbientLight() const {
    Index keyLightId{ 0 };
    if (!_currentFrame._ambientLights.empty()) {
        keyLightId = _currentFrame._ambientLights.front();
    }
    return _lights.get(keyLightId);
}

LightStage::ShadowPointer LightStage::getCurrentKeyShadow() const {
    Index keyLightId{ 0 };
    if (!_currentFrame._sunLights.empty()) {
        keyLightId = _currentFrame._sunLights.front();
    }
    auto shadow = getShadow(keyLightId);
    assert(shadow == nullptr || shadow->getLight() == getLight(keyLightId));
    return shadow;
}

LightStage::LightAndShadow LightStage::getCurrentKeyLightAndShadow() const {
    Index keyLightId{ 0 };
    if (!_currentFrame._sunLights.empty()) {
        keyLightId = _currentFrame._sunLights.front();
    }
    auto shadow = getShadow(keyLightId);
    auto light = getLight(keyLightId);
    assert(shadow == nullptr || shadow->getLight() == light);
    return LightAndShadow(light, shadow);
}

LightStage::Index LightStage::getShadowId(Index lightId) const {
    if (checkLightId(lightId)) {
        return _descs[lightId].shadowId;
    } else {
        return INVALID_INDEX;
    }
}

void LightStage::updateLightArrayBuffer(Index lightId) {
    auto lightSize = sizeof(model::Light::LightSchema);
    if (!_lightArrayBuffer) {
        _lightArrayBuffer = std::make_shared<gpu::Buffer>(lightSize);
    }

    assert(checkLightId(lightId));

    if (lightId > (Index)_lightArrayBuffer->getNumTypedElements<model::Light::LightSchema>()) {
        _lightArrayBuffer->resize(lightSize * (lightId + 10));
    }

    // lightArray is big enough so we can remap
    auto light = _lights._elements[lightId];
    if (light) {
        const auto& lightSchema = light->getLightSchemaBuffer().get();
        _lightArrayBuffer->setSubData<model::Light::LightSchema>(lightId, lightSchema);
    } else {
        // this should not happen ?
    }
}

LightStageSetup::LightStageSetup() {
}

void LightStageSetup::run(const render::RenderContextPointer& renderContext) {
    auto stage = renderContext->_scene->getStage(LightStage::getName());
    if (!stage) {
        stage = std::make_shared<LightStage>();
        renderContext->_scene->resetStage(LightStage::getName(), stage);
    }
}

