/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreStableHeaders.h"

#include "OgreHlmsPbsEs2.h"

#include "OgreHighLevelGpuProgramManager.h"
#include "OgreHighLevelGpuProgram.h"

#include "OgreSceneManager.h"
#include "Compositor/OgreCompositorShadowNode.h"

namespace Ogre
{
    HlmsPbsEs2::HlmsPbsEs2() : Hlms( mType, 0 )
    {
    }
    //-----------------------------------------------------------------------------------
    HlmsPbsEs2::~HlmsPbsEs2()
    {
    }
    //-----------------------------------------------------------------------------------
    HlmsCache HlmsPbsEs2::preparePassHash( const CompositorShadowNode *shadowNode, bool casterPass,
                                           bool dualParaboloid, SceneManager *sceneManager )
    {
        HlmsCache retVal = preparePassHash( shadowNode, casterPass, dualParaboloid, sceneManager );
        Camera *camera = sceneManager->getCameraInProgress();
        Matrix4 viewMatrix = camera->getViewMatrix(true);

        mPreparedPass.viewProjMatrix    = camera->getProjectionMatrix() * viewMatrix;
        mPreparedPass.viewMatrix        = viewMatrix;

        if( !casterPass )
        {
            int32 numShadowMaps = getProperty( HlmsPropertyNumShadowMaps );
            mPreparedPass.vertexShaderSharedBuffer.clear();
            mPreparedPass.vertexShaderSharedBuffer.reserve( (16 + 2) * numShadowMaps );

            //---------------------------------------------------------------------------
            //                          ---- VERTEX SHADER ----
            //---------------------------------------------------------------------------

            //mat4 texWorldViewProj[numShadowMaps]
            for( int32 i=0; i<numShadowMaps; ++i )
            {
                Matrix4 viewProjTex = shadowNode->getViewProjectionMatrix( i );
                for( size_t j=0; j<16; ++j )
                    mPreparedPass.vertexShaderSharedBuffer.push_back( (float)viewProjTex[0][j] );
            }
            //vec2 shadowDepthRange[numShadowMaps]
            for( int32 i=0; i<numShadowMaps; ++i )
            {
                Real fNear, fFar;
                shadowNode->getMinMaxDepthRange( i, fNear, fFar );
                const Real depthRange = fFar - fNear;
                mPreparedPass.vertexShaderSharedBuffer.push_back( fNear );
                mPreparedPass.vertexShaderSharedBuffer.push_back( 1.0f / depthRange );
            }

            //---------------------------------------------------------------------------
            //                          ---- PIXEL SHADER ----
            //---------------------------------------------------------------------------
            int32 numPssmSplits     = getProperty( HlmsPropertyPssmSplits );
            int32 numLights         = getProperty( HlmsPropertyLightsSpot );
            int32 numAttenLights    = getProperty( HlmsPropertyLightsAttenuation );
            int32 numSpotlights     = getProperty( HlmsPropertyLightsSpotParams );
            mPreparedPass.pixelShaderSharedBuffer.clear();
            mPreparedPass.pixelShaderSharedBuffer.reserve( 2 * numShadowMaps + numPssmSplits +
                                                           9 * numLights + 3 * numAttenLights +
                                                           6 * numSpotlights + 9 );

            Matrix3 viewMatrix3, invViewMatrix3;
            viewMatrix.extract3x3Matrix( viewMatrix3 );
            invViewMatrix3 = viewMatrix3.inverse();

            //vec2 invShadowMapSize
            for( int32 i=0; i<numShadowMaps; ++i )
            {
                //TODO: textures[0] is out of bounds when using shadow atlas. Also see how what
                //changes need to be done so that UV calculations land on the right place
                uint32 texWidth  = shadowNode->getLocalTextures()[i].textures[0]->getWidth();
                uint32 texHeight = shadowNode->getLocalTextures()[i].textures[0]->getHeight();
                mPreparedPass.pixelShaderSharedBuffer.push_back( 1.0f / texWidth );
                mPreparedPass.pixelShaderSharedBuffer.push_back( 1.0f / texHeight );
            }
            //float pssmSplitPoints
            for( int32 i=0; i<numPssmSplits; ++i )
                mPreparedPass.pixelShaderSharedBuffer.push_back( shadowNode->getPssmSplits(0)[i] );

            if( shadowNode )
            {
                const LightClosestArray &lights = shadowNode->getShadowCastingLights();
                //vec3 lightPosition[numLights]
                for( int32 i=0; i<numLights; ++i )
                {
                    Vector4 lightPos = lights[i].light->getAs4DVector();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( lightPos.x );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( lightPos.y );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( lightPos.z );
                }
                //vec3 lightDiffuse[numLights]
                for( int32 i=0; i<numLights; ++i )
                {
                    ColourValue colour = lights[i].light->getDiffuseColour() *
                                         lights[i].light->getPowerScale();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.r );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.g );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.b );
                }
                //vec3 lightSpecular[numLights]
                for( int32 i=0; i<numLights; ++i )
                {
                    ColourValue colour = lights[i].light->getSpecularColour() *
                                         lights[i].light->getPowerScale();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.r );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.g );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.b );
                }
                //vec3 attenuation[numAttenLights]
                for( int32 i=numLights - numAttenLights; i<numLights; ++i )
                {
                    Real attenRange     = lights[i].light->getAttenuationRange();
                    Real attenLinear    = lights[i].light->getAttenuationLinear();
                    Real attenQuadratic = lights[i].light->getAttenuationQuadric();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( attenRange );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( attenLinear );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( attenQuadratic );
                }
                //vec3 spotDirection[numSpotlights]
                for( int32 i=numLights - numSpotlights; i<numLights; ++i )
                {
                    Vector3 spotDir = viewMatrix3 * lights[i].light->getDerivedDirection();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( spotDir.x );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( spotDir.y );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( spotDir.z );
                }
                //vec3 spotParams[numSpotlights]
                for( int32 i=numLights - numSpotlights; i<numLights; ++i )
                {
                    Radian innerAngle = lights[i].light->getSpotlightInnerAngle();
                    Radian outerAngle = lights[i].light->getSpotlightOuterAngle();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( 1.0f /
                                        ( cosf( innerAngle.valueRadians() * 0.5f ) -
                                          cosf( outerAngle.valueRadians() * 0.5f ) ) );
                    mPreparedPass.pixelShaderSharedBuffer.push_back(
                                        cosf( outerAngle.valueRadians() * 0.5f ) );
                    mPreparedPass.pixelShaderSharedBuffer.push_back(
                                        lights[i].light->getSpotlightFalloff() );
                }
            }
            else
            {
                //No shadow maps, only pass directional lights
                const LightListInfo &globalLightList = sceneManager->getGlobalLightList();

                //vec3 lightPosition[numLights]
                for( int32 i=0; i<numLights; ++i )
                {
                    assert( globalLightList.lights[i]->getType() == Light::LT_DIRECTIONAL );
                    Vector4 lightPos = globalLightList.lights[i]->getAs4DVector();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( lightPos.x );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( lightPos.y );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( lightPos.z );
                }
                //vec3 lightDiffuse[numLights]
                for( int32 i=0; i<numLights; ++i )
                {
                    ColourValue colour = globalLightList.lights[i]->getDiffuseColour() *
                                         globalLightList.lights[i]->getPowerScale();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.r );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.g );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.b );
                }
                //vec3 lightSpecular[numLights]
                for( int32 i=0; i<numLights; ++i )
                {
                    ColourValue colour = globalLightList.lights[i]->getSpecularColour() *
                                         globalLightList.lights[i]->getPowerScale();
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.r );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.g );
                    mPreparedPass.pixelShaderSharedBuffer.push_back( colour.b );
                }
            }

            //mat3 invViewMat
            for( size_t i=0; i<9; ++i )
                mPreparedPass.pixelShaderSharedBuffer.push_back( (float)invViewMatrix3[0][i] );
        }
        else
        {
            mPreparedPass.vertexShaderSharedBuffer.clear();
            mPreparedPass.vertexShaderSharedBuffer.reserve( 2 );

            //vec2 depthRange;
            Real fNear, fFar;
            shadowNode->getMinMaxDepthRange( camera, fNear, fFar );
            const Real depthRange = fFar - fNear;
            mPreparedPass.vertexShaderSharedBuffer.push_back( fNear );
            mPreparedPass.vertexShaderSharedBuffer.push_back( 1.0f / depthRange );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    void HlmsPbsEs2::fillBuffersFor( const HlmsCache *cache, const QueuedRenderable &queuedRenderable,
                                     bool casterPass )
    {
        GpuProgramParametersSharedPtr vpParams = cache->vertexShader->getDefaultParameters();
        GpuProgramParametersSharedPtr psParams = cache->pixelShader->getDefaultParameters();
        float *vsUniformBuffer = vpParams->getFloatPointer( 0 );
        float *psUniformBuffer = psParams->getFloatPointer( 0 );

        //TODO: Check from Hash if we changed HlmsType, if so, rebind the textures.

        bool shadersChanged = true;
        if( shadersChanged )
        {
            memcpy( vsUniformBuffer, mPreparedPass.vertexShaderSharedBuffer.begin(),
                    sizeof(float) * mPreparedPass.vertexShaderSharedBuffer.size() );
            vsUniformBuffer += mPreparedPass.vertexShaderSharedBuffer.size();

            memcpy( psUniformBuffer, mPreparedPass.pixelShaderSharedBuffer.begin(),
                    sizeof(float) * mPreparedPass.pixelShaderSharedBuffer.size() );
            psUniformBuffer += mPreparedPass.pixelShaderSharedBuffer.size();
        }

        const Matrix4 &worldMat = queuedRenderable.movableObject->_getParentNodeFullTransform();
#if !OGRE_DOUBLE_PRECISION
        //mat4 mat4 worldView
        Matrix4 tmp = mPreparedPass.viewMatrix * worldMat;
        memcpy( vsUniformBuffer, &tmp, sizeof(Matrix4) );
        vsUniformBuffer += 16;
        //worldViewProj
        tmp = mPreparedPass.viewProjMatrix * worldMat;
        memcpy( vsUniformBuffer, &tmp, sizeof(Matrix4) );
        vsUniformBuffer += 16;
#else
    #error Not Coded Yet! (can't use memcpy on Matrix4)
#endif
        //queuedRenderable.renderable->getWorldTransforms();
        //vsUniformBuffer = queuedRenderable;

        //vsUniformBuffer worldViewProj
    }
}