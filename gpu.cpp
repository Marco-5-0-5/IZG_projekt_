/*!
 * @file
 * @brief This file contains implementation of gpu
 *
 * @author Tomáš Milet, imilet@fit.vutbr.cz
 * 
 */
//author 2 Marco Suga, xsugama00@vutbr.cz

#include <studentSolution/gpu.hpp>
#include <algorithm>
#include <cmath>

static void rasterizeTriangle(GPUMemory& mem, Program const& prog, OutVertex const& v0, OutVertex const& v1, OutVertex const& v2);

void readAttributes(GPUMemory const& mem, VertexArray const& vao,
    InVertex& inVertex, uint32_t vertexID) {
    for (uint32_t a = 0; a < maxAttribs; a++) {
        auto& attrib = vao.vertexAttrib[a];
        if (attrib.bufferID < 0) continue;
        if (attrib.type == AttribType::EMPTY) continue;

        uint8_t* ptr = (uint8_t*)mem.buffers[attrib.bufferID].data
            + attrib.offset
            + (uint64_t)vertexID * attrib.stride;

        if (attrib.type == AttribType::FLOAT) inVertex.attributes[a].v1 = *(float*)ptr;
        else if (attrib.type == AttribType::VEC2)  inVertex.attributes[a].v2 = *(glm::vec2*)ptr;
        else if (attrib.type == AttribType::VEC3)  inVertex.attributes[a].v3 = *(glm::vec3*)ptr;
        else if (attrib.type == AttribType::VEC4)  inVertex.attributes[a].v4 = *(glm::vec4*)ptr;
        else if (attrib.type == AttribType::UINT)  inVertex.attributes[a].u1 = *(uint32_t*)ptr;
        else if (attrib.type == AttribType::UVEC2) inVertex.attributes[a].u2 = *(glm::uvec2*)ptr;
        else if (attrib.type == AttribType::UVEC3) inVertex.attributes[a].u3 = *(glm::uvec3*)ptr;
        else if (attrib.type == AttribType::UVEC4) inVertex.attributes[a].u4 = *(glm::uvec4*)ptr;
    }
}


static void applyStencilOp(uint8_t& s, StencilOp op, uint32_t ref) {
    switch (op) {
    case StencilOp::KEEP:     break;
    case StencilOp::ZERO:      s = 0;        break;
    case StencilOp::REPLACE:   s = (uint8_t)ref; break;
    case StencilOp::INCR:      if (s < 255) s++; break;
    case StencilOp::INCR_WRAP: s++;          break;
    case StencilOp::DECR:      if (s > 0)   s--; break;
    case StencilOp::DECR_WRAP: s--;          break;
    case StencilOp::INVERT:    s = ~s;       break;
    }
}

static glm::vec3 perspectiveDivide(OutVertex const& v) {
    return glm::vec3(
        v.gl_Position.x / v.gl_Position.w,
        v.gl_Position.y / v.gl_Position.w,
        v.gl_Position.z / v.gl_Position.w
    );
}

static glm::vec2 ndcToScreen(Framebuffer const& fb, glm::vec3 const& ndc) {
    float x = (ndc.x * 0.5f + 0.5f) * float(fb.width);
    float y = (ndc.y * 0.5f + 0.5f) * float(fb.height);
    if (fb.yReversed) y = float(fb.height) - y;
    return glm::vec2(x, y);
}

static float edgeFunction(glm::vec2 const& a, glm::vec2 const& b, glm::vec2 const& p) {
    glm::vec2 ab = b - a;
    glm::vec2 ap = p - a;
    return ab.x * ap.y - ab.y * ap.x;
}


static OutVertex lerpVertex(OutVertex const& a, OutVertex const& b, float t) {
    OutVertex r;
    r.gl_Position = glm::mix(a.gl_Position, b.gl_Position, t);
    for (uint32_t i = 0; i < maxAttribs; ++i)
        r.attributes[i].v4 = glm::mix(a.attributes[i].v4, b.attributes[i].v4, t);
    return r;
}

static void clipAndRasterize(GPUMemory& mem, Program const& prog,
    OutVertex const& v0, OutVertex const& v1, OutVertex const& v2)
{
    auto inside = [](OutVertex const& v) {
        return v.gl_Position.z >= -v.gl_Position.w;
        };
    auto intersect = [](OutVertex const& a, OutVertex const& b) -> OutVertex {
        float da = a.gl_Position.z + a.gl_Position.w;
        float db = b.gl_Position.z + b.gl_Position.w;
        float t = da / (da - db);
        return lerpVertex(a, b, t);
        };

    OutVertex tri[3] = { v0, v1, v2 };
    OutVertex out[4];
    int n = 0;

    for (int i = 0; i < 3; ++i) {
        OutVertex const& cur = tri[i];
        OutVertex const& nxt = tri[(i + 1) % 3];
        bool curIn = inside(cur);
        bool nxtIn = inside(nxt);
        if (curIn) out[n++] = cur;
        if (curIn != nxtIn) out[n++] = intersect(cur, nxt);
    }

    for (int i = 1; i + 1 < n; ++i)
        rasterizeTriangle(mem, prog, out[0], out[i], out[i + 1]);
}

static glm::vec4 readPixelColor(Image const& img, uint32_t x, uint32_t y) {
    glm::vec4 color(0.f, 0.f, 0.f, 1.f);
    if (img.format == Image::F32) {
        auto* p = (float*)getPixel(img, x, y);
        for (uint32_t c = 0; c < img.channels; ++c) {
            switch (img.channelTypes[c]) {
            case Image::Channel::RED:   color.r = p[c]; break;
            case Image::Channel::GREEN: color.g = p[c]; break;
            case Image::Channel::BLUE:  color.b = p[c]; break;
            case Image::Channel::ALPHA: color.a = p[c]; break;
            }
        }
    }
    else {
        auto* p = (uint8_t*)getPixel(img, x, y);
        for (uint32_t c = 0; c < img.channels; ++c) {
            switch (img.channelTypes[c]) {
            case Image::Channel::RED:   color.r = p[c] / 255.f; break;
            case Image::Channel::GREEN: color.g = p[c] / 255.f; break;
            case Image::Channel::BLUE:  color.b = p[c] / 255.f; break;
            case Image::Channel::ALPHA: color.a = p[c] / 255.f; break;
            }
        }
    }
    return color;
}

static void writePixelColor(Image& img, uint32_t x, uint32_t y, glm::vec4 const& col) {
    if (img.format == Image::F32) {
        auto* p = (float*)getPixel(img, x, y);
        for (uint32_t c = 0; c < img.channels; ++c) {
            switch (img.channelTypes[c]) {
            case Image::Channel::RED:   p[c] = col.r; break;
            case Image::Channel::GREEN: p[c] = col.g; break;
            case Image::Channel::BLUE:  p[c] = col.b; break;
            case Image::Channel::ALPHA: p[c] = col.a; break;
            }
        }
    }
    else {
        auto* p = (uint8_t*)getPixel(img, x, y);
        for (uint32_t c = 0; c < img.channels; ++c) {
            switch (img.channelTypes[c]) {
            case Image::Channel::RED:   p[c] = (uint8_t)(col.r * 255.f); break;
            case Image::Channel::GREEN: p[c] = (uint8_t)(col.g * 255.f); break;
            case Image::Channel::BLUE:  p[c] = (uint8_t)(col.b * 255.f); break;
            case Image::Channel::ALPHA: p[c] = (uint8_t)(col.a * 255.f); break;
            }
        }
    }
}


static void rasterizeTriangle(GPUMemory& mem, Program const& prog, OutVertex const& v0, OutVertex const& v1, OutVertex const& v2) {
    auto& fb = mem.framebuffers[mem.activatedFramebuffer];
    if (fb.width == 0 || fb.height == 0) return;

    glm::vec3 ndc0 = perspectiveDivide(v0);
    glm::vec3 ndc1 = perspectiveDivide(v1);
    glm::vec3 ndc2 = perspectiveDivide(v2);


    glm::vec2 p0 = ndcToScreen(fb, ndc0);
    glm::vec2 p1 = ndcToScreen(fb, ndc1);
    glm::vec2 p2 = ndcToScreen(fb, ndc2);

    
    if (mem.backfaceCulling.enabled) {
        float signedArea = edgeFunction(p0, p1, p2);
        bool isCCW = signedArea > 0.0f;
        if (mem.backfaceCulling.frontFaceIsCounterClockWise) {
            if (!isCCW) return;
        }
        else {
            if (isCCW) return;
        }
    }

    float minXf = std::min({ p0.x, p1.x, p2.x });
    float maxXf = std::max({ p0.x, p1.x, p2.x });
    float minYf = std::min({ p0.y, p1.y, p2.y });
    float maxYf = std::max({ p0.y, p1.y, p2.y });

    int32_t minX = std::max(0, (int32_t)std::floor(minXf));
    int32_t maxX = std::min((int32_t)fb.width - 1, (int32_t)std::ceil(maxXf));
    int32_t minY = std::max(0, (int32_t)std::floor(minYf));
    int32_t maxY = std::min((int32_t)fb.height - 1, (int32_t)std::ceil(maxYf));

    float area = edgeFunction(p0, p1, p2);
    if (area == 0.0f) return;

    float invW0 = 1.0f / v0.gl_Position.w;
    float invW1 = 1.0f / v1.gl_Position.w;
    float invW2 = 1.0f / v2.gl_Position.w;

    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            glm::vec2 p = glm::vec2(float(x) + 0.5f, float(y) + 0.5f);

            float e0 = edgeFunction(p1, p2, p);
            float e1 = edgeFunction(p2, p0, p);
            float e2 = edgeFunction(p0, p1, p);

            bool inside = (e0 > 0 && e1 > 0 && e2 > 0) || (e0 < 0 && e1 < 0 && e2 < 0);
            if (!inside) continue;

            float w0 = e0 / area;
            float w1 = e1 / area;
            float w2 = e2 / area;

            float perspW = 1.0f / (w0 * invW0 + w1 * invW1 + w2 * invW2);
            float c0 = w0 * invW0 * perspW;
            float c1 = w1 * invW1 * perspW;
            float c2 = w2 * invW2 * perspW;

            InFragment inFrag;
            inFrag.gl_FragCoord = glm::vec4(
                float(x) + 0.5f,
                float(y) + 0.5f,
                w0 * ndc0.z + w1 * ndc1.z + w2 * ndc2.z,
                1.0f / perspW
            );

            for (uint32_t a = 0; a < maxAttribs; ++a) {
                if (prog.vs2fs[a] == AttribType::EMPTY) continue;
                switch (prog.vs2fs[a]) {
                case AttribType::FLOAT:
                    inFrag.attributes[a].v1 = c0 * v0.attributes[a].v1 + c1 * v1.attributes[a].v1 + c2 * v2.attributes[a].v1;
                    break;
                case AttribType::VEC2:
                    inFrag.attributes[a].v2 = c0 * v0.attributes[a].v2 + c1 * v1.attributes[a].v2 + c2 * v2.attributes[a].v2;
                    break;
                case AttribType::VEC3:
                    inFrag.attributes[a].v3 = c0 * v0.attributes[a].v3 + c1 * v1.attributes[a].v3 + c2 * v2.attributes[a].v3;
                    break;
                case AttribType::VEC4:
                    inFrag.attributes[a].v4 = c0 * v0.attributes[a].v4 + c1 * v1.attributes[a].v4 + c2 * v2.attributes[a].v4;
                    break;
                case AttribType::UINT:
                    inFrag.attributes[a].u1 = v0.attributes[a].u1;
                    break;
                case AttribType::UVEC2:
                    inFrag.attributes[a].u2 = v0.attributes[a].u2;
                    break;
                case AttribType::UVEC3:
                    inFrag.attributes[a].u3 = v0.attributes[a].u3;
                    break;
                case AttribType::UVEC4:
                    inFrag.attributes[a].u4 = v0.attributes[a].u4;
                    break;
                default:
                    break;
                }
            }

            
            StencilOps const& sops = mem.backfaceCulling.frontFaceIsCounterClockWise
                ? (area > 0.f ? mem.stencilSettings.frontOps : mem.stencilSettings.backOps)
                : (area < 0.f ? mem.stencilSettings.frontOps : mem.stencilSettings.backOps);

            if (mem.stencilSettings.enabled && fb.stencil.data) {
                bool stencilPass = false;
                uint8_t sVal = 0;
                if (fb.stencil.data) {
                    auto* s = (uint8_t*)getPixel(fb.stencil, (uint32_t)x, (uint32_t)y);
                    sVal = *s;
                }

                uint8_t sRef = (uint8_t)mem.stencilSettings.refValue;
                switch (mem.stencilSettings.func) {
                case StencilFunc::NEVER:    stencilPass = false;          break;
                case StencilFunc::ALWAYS:   stencilPass = true;           break;
                case StencilFunc::EQUAL:    stencilPass = (sVal == sRef); break;
                case StencilFunc::NOTEQUAL: stencilPass = (sVal != sRef); break;
                case StencilFunc::LESS:     stencilPass = (sVal < sRef);  break;
                case StencilFunc::LEQUAL:   stencilPass = (sVal <= sRef); break;
                case StencilFunc::GREATER:  stencilPass = (sVal > sRef);  break;
                case StencilFunc::GEQUAL:   stencilPass = (sVal >= sRef); break;
                }

                if (!stencilPass) {
                    if (fb.stencil.data && !mem.blockWrites.stencil) {
                        auto* s = (uint8_t*)getPixel(fb.stencil, (uint32_t)x, (uint32_t)y);
                        applyStencilOp(*s, sops.sfail, mem.stencilSettings.refValue);
                    }
                    continue;
                }
            }


            bool depthPass = true;
            if (fb.depth.data) {
                auto* d = (float*)getPixel(fb.depth, (uint32_t)x, (uint32_t)y);
                if (inFrag.gl_FragCoord.z > *d) {
                    depthPass = false;
                    if (mem.stencilSettings.enabled && fb.stencil.data && !mem.blockWrites.stencil) {
                        auto* s = (uint8_t*)getPixel(fb.stencil, (uint32_t)x, (uint32_t)y);
                        applyStencilOp(*s, sops.dpfail, mem.stencilSettings.refValue);
                    }
                }
            }

            if (!depthPass) continue;
            
            OutFragment outFrag{};
            outFrag.discard = false;
            if (prog.fragmentShader)
                prog.fragmentShader(outFrag, inFrag, ShaderInterface{mem.uniforms, mem.textures, mem.gl_DrawID });
            if (outFrag.discard) continue;




            if (mem.stencilSettings.enabled && fb.stencil.data && !mem.blockWrites.stencil) {
                auto* s = (uint8_t*)getPixel(fb.stencil, (uint32_t)x, (uint32_t)y);
                applyStencilOp(*s, sops.dppass, mem.stencilSettings.refValue);
            }

            if (fb.depth.data && !mem.blockWrites.depth) {
                auto* d = (float*)getPixel(fb.depth, (uint32_t)x, (uint32_t)y);
                *d = inFrag.gl_FragCoord.z;
            }

            if (mem.blockWrites.color) continue;
            if (!fb.color.data) continue;

            glm::vec4 fragColor = outFrag.gl_FragColor;
            glm::vec4 result;

            if (!mem.blendingSettings.enabled) {
                result = fragColor;
            }
            else {
                glm::vec4 frameColor = readPixelColor(fb.color, (uint32_t)x, (uint32_t)y);

                auto calcFactor = [&](BlendFunc func) -> glm::vec4 {
                    switch (func) {
                    case BlendFunc::ZERO:                return glm::vec4(0.f);
                    case BlendFunc::ONE:                 return glm::vec4(1.f);
                    case BlendFunc::SRC_COLOR:           return fragColor;
                    case BlendFunc::ONE_MINUS_SRC_COLOR: return glm::vec4(1.f) - fragColor;
                    case BlendFunc::DST_COLOR:           return frameColor;
                    case BlendFunc::ONE_MINUS_DST_COLOR: return glm::vec4(1.f) - frameColor;
                    case BlendFunc::SRC_ALPHA:           return glm::vec4(fragColor.a);
                    case BlendFunc::ONE_MINUS_SRC_ALPHA: return glm::vec4(1.f - fragColor.a);
                    case BlendFunc::DST_ALPHA:           return glm::vec4(frameColor.a);
                    case BlendFunc::ONE_MINUS_DST_ALPHA: return glm::vec4(1.f - frameColor.a);
                    default:                             return glm::vec4(1.f);
                    }
                    };

                glm::vec4 s = calcFactor(mem.blendingSettings.sFactor);
                glm::vec4 d = calcFactor(mem.blendingSettings.dFactor);

                switch (mem.blendingSettings.equation) {
                case BlendEquation::ADD:              result = fragColor * s + frameColor * d; break;
                case BlendEquation::SUBTRACT:         result = fragColor * s - frameColor * d; break;
                case BlendEquation::REVERSE_SUBTRACT: result = frameColor * d - fragColor * s; break;
                case BlendEquation::MIN:              result = glm::min(fragColor, frameColor);  break;
                case BlendEquation::MAX:              result = glm::max(fragColor, frameColor);  break;
                default:                              result = fragColor; break;
                }
            }

            writePixelColor(fb.color, (uint32_t)x, (uint32_t)y, result);
        }
    }
}


void drawCommand(GPUMemory& mem, DrawCommand const& cmd) {
    auto& prog = mem.programs[mem.activatedProgram];
    auto& vao = mem.vertexArrays[mem.activatedVertexArray];

    OutVertex triangle[3];

    for (uint32_t i = 0; i < cmd.nofVertices; i++) {
        InVertex  inVertex;
        OutVertex outVertex;
        ShaderInterface si;

        if (vao.indexBufferID >= 0) {
            uint8_t* base = (uint8_t*)mem.buffers[vao.indexBufferID].data + vao.indexOffset;
            if (vao.indexType == IndexType::U8)        inVertex.gl_VertexID = ((uint8_t*)base)[i];
            else if (vao.indexType == IndexType::U16)  inVertex.gl_VertexID = ((uint16_t*)base)[i];
            else                                       inVertex.gl_VertexID = ((uint32_t*)base)[i];
        }
        else {
            inVertex.gl_VertexID = i;
        }

        readAttributes(mem, vao, inVertex, inVertex.gl_VertexID);

        si.uniforms = mem.uniforms;
        si.textures = mem.textures;
        si.gl_DrawID = mem.gl_DrawID; 

        if (prog.vertexShader)
            prog.vertexShader(outVertex, inVertex, si);

        triangle[i % 3] = outVertex;

        if (i % 3 == 2) {
            clipAndRasterize(mem, prog, triangle[0], triangle[1], triangle[2]);
        }
    }
}



void student_GPU_run(GPUMemory& mem, CommandBuffer const& cb) {
	mem.gl_DrawID = 0;

    for (uint32_t i = 0; i < cb.nofCommands; i++) {
        auto const& cmd = cb.commands[i];
        switch (cmd.type) {

        case CommandType::BIND_FRAMEBUFFER:
            mem.activatedFramebuffer = cmd.data.bindFramebufferCommand.id;
            break;

        case CommandType::BIND_PROGRAM:
            mem.activatedProgram = cmd.data.bindProgramCommand.id;
            break;

        case CommandType::BIND_VERTEXARRAY:
            mem.activatedVertexArray = cmd.data.bindVertexArrayCommand.id;
            break;

        case CommandType::SET_DRAW_ID:
            mem.gl_DrawID = cmd.data.setDrawIdCommand.id;
            break;

        case CommandType::BLOCK_WRITES_COMMAND:
            mem.blockWrites = cmd.data.blockWritesCommand.blockWrites;
            break;

        case CommandType::SET_BACKFACE_CULLING_COMMAND:
            mem.backfaceCulling.enabled = cmd.data.setBackfaceCullingCommand.enabled;
            break;

        case CommandType::SET_FRONT_FACE_COMMAND:
            mem.backfaceCulling.frontFaceIsCounterClockWise = cmd.data.setFrontFaceCommand.frontFaceIsCounterClockWise;
            break;

        case CommandType::SET_STENCIL_COMMAND:
            mem.stencilSettings = cmd.data.setStencilCommand.settings;
            break;

        case CommandType::CLEAR_COLOR: {
            auto& fb = mem.framebuffers[mem.activatedFramebuffer];
            if (!fb.color.data) break;
            glm::vec4 clearVal = cmd.data.clearColorCommand.value;
            for (uint32_t y = 0; y < fb.height; y++)
                for (uint32_t x = 0; x < fb.width; x++)
                    writePixelColor(fb.color, x, y, clearVal);
            break;
        }

        case CommandType::CLEAR_DEPTH: {
            auto& fb = mem.framebuffers[mem.activatedFramebuffer];
            auto& img = fb.depth;
            if (!img.data) break;
            for (uint32_t y = 0; y < fb.height; y++) {
                for (uint32_t x = 0; x < fb.width; x++) {
                    auto* pixel = (float*)getPixel(img, x, y);
                    *pixel = cmd.data.clearDepthCommand.value;
                }
            }
            break;
        }

        case CommandType::CLEAR_STENCIL: {
            auto& fb = mem.framebuffers[mem.activatedFramebuffer];
            auto& img = fb.stencil;
            if (!img.data) break;
            for (uint32_t y = 0; y < fb.height; y++) {
                for (uint32_t x = 0; x < fb.width; x++) {
                    auto* pixel = (uint8_t*)getPixel(img, x, y);
                    *pixel = cmd.data.clearStencilCommand.value;
                }
            }
            break;
        }

        case CommandType::USER_COMMAND: {
            auto& uc = cmd.data.userCommand;
            if (uc.callback)
                uc.callback(uc.data);
            break;
        }

        case CommandType::SUB_COMMAND: {
            auto& sub = cmd.data.subCommand;
            if (sub.commandBuffer) {
                uint32_t savedDrawID = mem.gl_DrawID;
                student_GPU_run(mem, *sub.commandBuffer);
                mem.gl_DrawID = savedDrawID;         
            }
            break;
        }

        case CommandType::DRAW: {
            drawCommand(mem, cmd.data.drawCommand);
            mem.gl_DrawID++;  
            break;
        }



         case CommandType::SET_BLENDING_COMMAND:
            mem.blendingSettings = cmd.data.setBlendingCommand.settings;
            break;

        default:
            break;
        }
    }
}