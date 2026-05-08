/*!
 * @file
 * @brief This file contains implementation of gpu
 *
 * @author Tomáš Milet, imilet@fit.vutbr.cz
 */

#include <studentSolution/gpu.hpp>



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


void drawCommand(GPUMemory& mem, DrawCommand const& cmd) {
    auto& prog = mem.programs[mem.activatedProgram];
    auto& vao = mem.vertexArrays[mem.activatedVertexArray];


    OutVertex triangle[3];

    for (uint32_t i = 0; i < cmd.nofVertices; i++) {
        InVertex  inVertex;
        OutVertex outVertex;
        ShaderInterface si;

        // glVertexID — ak je indexovanie zapnuté, čítaj z index bufferu
        if (vao.indexBufferID >= 0) {
            uint8_t* base = (uint8_t*)mem.buffers[vao.indexBufferID].data + vao.indexOffset;
            if (vao.indexType == IndexType::U8)
                inVertex.gl_VertexID = ((uint8_t*)base)[i];
            else if (vao.indexType == IndexType::U16)
                inVertex.gl_VertexID = ((uint16_t*)base)[i];
            else
                inVertex.gl_VertexID = ((uint32_t*)base)[i];
        }
        else {
            inVertex.gl_VertexID = i;
        }

        readAttributes(mem, vao, inVertex, inVertex.gl_VertexID);

        si.uniforms = mem.uniforms;
        si.textures = mem.textures;
        si.gl_DrawID = mem.glDrawID - 1;

        if (prog.vertexShader)
            prog.vertexShader(outVertex, inVertex, si);


        triangle[i % 3] = outVertex;

        if (i % 3 == 2) {

        }
    }
}

//! [student_GPU_run]
void student_GPU_run(GPUMemory& mem, CommandBuffer const& cb) {
    (void)mem;
    (void)cb;
    /// \todo Tato funkce reprezentuje funkcionalitu grafické karty.<br>
    /// Měla by umět zpracovat command buffer, čistit framebuffer a kreslit.<br>
    /// mem obsahuje paměť grafické karty.
    /// cb obsahuje command buffer pro zpracování.
    /// Bližší informace jsou uvedeny na hlavní stránce dokumentace.
    ///
    /// V základu jde o to, že cb obsahuje příkazy, které se musí provést nad pamětí mem.
    /// Správně fungující grafická karta dobře interpretuje příkazy v cb a správně změní obsah paměti mem.

    // Loop od 0 po počet príkazov
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
            auto& img = fb.color;
            if (!img.data) break;
            for (uint32_t y = 0; y < fb.height; y++) {
                for (uint32_t x = 0; x < fb.width; x++) {
                    if (img.format == Image::F32) {
                        auto* pixel = (float*)getPixel(img, x, y);
                        for (uint32_t c = 0; c < img.channels; c++)
                            pixel[c] = cmd.data.clearColorCommand.value[c];
                    }
                    else { // U8
                        auto* pixel = (uint8_t*)getPixel(img, x, y);
                        for (uint32_t c = 0; c < img.channels; c++)
                            pixel[c] = (uint8_t)(cmd.data.clearColorCommand.value[c] * 255.f);
                    }
                }
            }
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
            if (sub.commandBuffer)
                student_GPU_run(mem, *sub.commandBuffer);
            break;
        }

        case CommandType::DRAW: {
            mem.glDrawID++;
            drawCommand(mem, cmd.data.drawCommand);
            break;
        }


        default:
            break;
        }
    }





}
//! [student_GPU_run]

