/*!
* @file
* @brief This file contains functions for model rendering
*
* @author Tomáš Milet, imilet@fit.vutbr.cz
*/
#include <studentSolution/prepareModel.hpp>
#include <studentSolution/gpu.hpp>
#include <solutionInterface/uniformLocations.hpp>
#include <studentSolution/shaderFunctions.hpp>


void prepareNode(
	GPUMemory& mem,
	CommandBuffer& cb,
	Model const& model,
	Node const& node,
	glm::mat4 const& parentMatrix,
	uint32_t& drawId)
{
	glm::mat4 modelMatrix = parentMatrix * node.modelMatrix;

	if (node.mesh >= 0) {
		Mesh const& mesh = model.meshes[node.mesh];

		mem.vertexArrays[drawId].vertexAttrib[0] = mesh.position;
		mem.vertexArrays[drawId].vertexAttrib[1] = mesh.normal;
		mem.vertexArrays[drawId].vertexAttrib[2] = mesh.texCoord;
		mem.vertexArrays[drawId].indexBufferID = mesh.indexBufferID;
		mem.vertexArrays[drawId].indexOffset = mesh.indexOffset;
		mem.vertexArrays[drawId].indexType = mesh.indexType;

		mem.uniforms[getUniformLocation(drawId, MODEL_MATRIX)].m4 = modelMatrix;
		mem.uniforms[getUniformLocation(drawId, INVERSE_TRANSPOSE_MODEL_MATRIX)].m4 =
			glm::transpose(glm::inverse(modelMatrix));
		mem.uniforms[getUniformLocation(drawId, DIFFUSE_COLOR)].v4 = mesh.diffuseColor;
		mem.uniforms[getUniformLocation(drawId, TEXTURE_ID)].i1 = mesh.diffuseTexture;
		mem.uniforms[getUniformLocation(drawId, DOUBLE_SIDED)].v1 = mesh.doubleSided ? 1.f : 0.f;

		pushBindVertexArrayCommand(cb, drawId);
		pushSetBackfaceCullingCommand(cb, !mesh.doubleSided);
		pushDrawCommand(cb, mesh.nofIndices);

		drawId++;
	}

	for (size_t i = 0;i < node.nofChildren;++i)
		prepareNode(mem, cb, model, node.children[i], modelMatrix, drawId);
}


/**
* @brief This function prepares model into memory and creates command buffer
*
* @param mem gpu memory
* @param commandBuffer command buffer
* @param model model structure
*/
//! [drawModel]
void student_prepareModel(GPUMemory& mem, CommandBuffer& commandBuffer, Model const& model) {
	commandBuffer.nofCommands = 0;

	for (size_t i = 0;i < model.nofBuffers;++i) mem.buffers[i] = model.buffers[i];
	for (size_t i = 0;i < model.nofTextures;++i) mem.textures[i] = model.textures[i];


	uint32_t drawId = 0;
	for (size_t i = 0;i < model.nofRoots;++i)
		prepareNode(mem, commandBuffer, model, model.roots[i], glm::mat4(1.f), drawId);
}
/// \todo Tato funkce připraví command buffer pro model a nastaví správně pamět grafické karty.<br>
/// Vaším úkolem je správně projít model a vložit vykreslovací příkazy do commandBufferu.
/// Zároveň musíte vložit do paměti textury, buffery a uniformní proměnné, které buffer command buffer využívat.
/// Bližší informace jsou uvedeny na hlavní stránce dokumentace a v testech.
//! [drawModel]

/**
* @brief This function represents vertex shader of texture rendering method.
*
* @param outVertex output vertex
* @param inVertex input vertex
* @param si shader interface
*/
//! [drawModel_vs]
void student_drawModel_vertexShader(OutVertex& outVertex, InVertex const& inVertex, ShaderInterface const& si) {
	glm::vec3 const pos = inVertex.attributes[0].v3;
	glm::vec3 const nor = inVertex.attributes[1].v3;
	glm::vec2 const uv = inVertex.attributes[2].v2;

	glm::mat4 const model = si.uniforms[getUniformLocation(si.gl_DrawID, MODEL_MATRIX)].m4;
	glm::mat4 const invTrModel = si.uniforms[getUniformLocation(si.gl_DrawID, INVERSE_TRANSPOSE_MODEL_MATRIX)].m4;
	glm::mat4 const projView = si.uniforms[PROJECTION_VIEW_MATRIX].m4;

	glm::vec4 worldPos = model * glm::vec4(pos, 1.f);

	outVertex.gl_Position = projView * worldPos;
	outVertex.attributes[0].v3 = glm::vec3(worldPos);
	outVertex.attributes[1].v3 = glm::vec3(invTrModel * glm::vec4(nor, 0.f));
	outVertex.attributes[2].v2 = uv;
	glm::mat4 const lightProjView = si.uniforms[getUniformLocation(si.gl_DrawID, USE_SHADOW_MAP_MATRIX)].m4;
	outVertex.attributes[3].v4 = lightProjView * worldPos;
}

/// \todo Tato funkce reprezentujte vertex shader.<br>
/// Vaším úkolem je správně trasnformovat vrcholy modelu.
/// Bližší informace jsou uvedeny na hlavní stránce dokumentace.
//! [drawModel_vs]

#include<iostream>

/**
* @brief This functionrepresents fragment shader of texture rendering method.
*
* @param outFragment output fragment
* @param inFragment input fragment
* @param si shader interface
*/
//! [drawModel_fs]
void student_drawModel_fragmentShader(OutFragment& outFragment, InFragment const& inFragment, ShaderInterface const& si) {
	uint32_t drawId = si.gl_DrawID;

	glm::vec3 position = inFragment.attributes[0].v3;
	glm::vec3 normal = glm::normalize(inFragment.attributes[1].v3);
	glm::vec2 uv = inFragment.attributes[2].v2;

	glm::vec4 diffuseColor = si.uniforms[getUniformLocation(drawId, DIFFUSE_COLOR)].v4;
	int textureId = si.uniforms[getUniformLocation(drawId, TEXTURE_ID)].i1;
	float doubleSided = si.uniforms[getUniformLocation(drawId, DOUBLE_SIDED)].v1;

	glm::vec3 lightPos = si.uniforms[LIGHT_POSITION].v3;
	glm::vec3 cameraPos = si.uniforms[CAMERA_POSITION].v3;
	glm::vec3 ambientLight = si.uniforms[AMBIENT_LIGHT_COLOR].v3;
	glm::vec3 lightColor = si.uniforms[LIGHT_COLOR].v3;

	if (textureId >= 0) diffuseColor = student_read_texture(si.textures[textureId], uv);

	if (diffuseColor.a < 0.5f) {
		outFragment.discard = true;
		return;
	}

	if (doubleSided > 0.f) {
		glm::vec3 viewDir = position - cameraPos;
		if (glm::dot(viewDir, normal) > 0.f) normal = -normal;
	}

	glm::vec3 L = glm::normalize(lightPos - position);
	float diffF = glm::clamp(glm::dot(L, normal), 0.f, 1.f);

	glm::vec3 baseColor = glm::vec3(diffuseColor);
	glm::vec3 ambient = baseColor * ambientLight;
	glm::vec3 diffuse = baseColor * lightColor * diffF;
	outFragment.gl_FragColor = glm::vec4(ambient + diffuse, diffuseColor.a);
}
/// \todo Tato funkce reprezentujte fragment shader.<br>
/// Vašim úkolem je správně obarvit fragmenty a osvětlit je pomocí lambertova osvětlovacího modelu.
/// Bližší informace jsou uvedeny na hlavní stránce dokumentace.
//! [drawModel_fs]