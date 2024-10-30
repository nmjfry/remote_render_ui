#include "ModelViewer.h"

void  ModelViewer::setupMesh(Mesh& mesh) {
    // Generate and bind VAO
    glGenVertexArrays(1, &mesh.VAO);
    glBindVertexArray(mesh.VAO);

    // Generate VBO and load vertex data
    glGenBuffers(1, &mesh.VBO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(float), &mesh.vertices[0], GL_STATIC_DRAW);

    // Generate EBO and load index data
    glGenBuffers(1, &mesh.EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), &mesh.indices[0], GL_STATIC_DRAW);

    // Assuming vertex format is (x, y, z), set vertex attribute pointers
    glEnableVertexAttribArray(0); // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Unbind VAO to prevent accidental modifications
    glBindVertexArray(0);
}

bool ModelViewer::setupShaders() {
    const char* vertexShaderSource = R"(
            #version 330 core
            layout (location = 0) in vec3 aPos;
            
            uniform mat4 model;
            uniform mat4 view;
            uniform mat4 projection;
            
            void main() {
                gl_Position = projection * view * model * vec4(aPos, 1.0);
            }
        )";
        
        const char* fragmentShaderSource = R"(
            #version 330 core
            out vec4 FragColor;
            
            void main() {
                FragColor = vec4(0.8, 0.8, 0.8, 1.0); // Gray color
            }
        )";


        
        // Compile vertex shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShader);
        
        // Check for compilation errors
        int success;
        char infoLog[512];
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            printf("Vertex shader compilation failed: %s\n", infoLog);
            return false;
        }
        
        // Compile fragment shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
        glCompileShader(fragmentShader);
        
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
            printf("Fragment shader compilation failed: %s\n", infoLog);
            return false;
        }
        
        // Link shaders
        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
            printf("Shader program linking failed: %s\n", infoLog);
            return false;
        }
        
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        
        // Get uniform locations
        modelLoc = glGetUniformLocation(shaderProgram, "model");
        viewLoc = glGetUniformLocation(shaderProgram, "view");
        projectionLoc = glGetUniformLocation(shaderProgram, "projection");
        

        return true;
}

bool ModelViewer::loadModel(const std::string& path) {

    printf("Loading model: %s\n", path.c_str());

        // Check if we have a valid OpenGL context
    if (glfwGetCurrentContext() == nullptr) {
        throw std::runtime_error("No OpenGL context found. Initialize OpenGL before creating ModelViewer.");
    }

    // Initialize shaders
    if (!setupShaders()) {
        throw std::runtime_error("Failed to setup shaders");
    }



    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, 
        aiProcess_Triangulate | aiProcess_GenNormals);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        printf("Error loading model: %s\n", importer.GetErrorString());
        return false;
    }
    
    meshes.clear();
    
    // Process each mesh in the scene
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        Mesh newMesh;
        
        // Get vertices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            newMesh.vertices.push_back(mesh->mVertices[j].x);
            newMesh.vertices.push_back(mesh->mVertices[j].y);
            newMesh.vertices.push_back(mesh->mVertices[j].z);
        }
        
        // Get indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                newMesh.indices.push_back(face.mIndices[k]);
            }
        }
        
        setupMesh(newMesh);
        meshes.push_back(newMesh);
    }
    
    modelLoaded = true;
    return true;
}

void ModelViewer::ShowModelWindow(bool* p_open) {

    if (ImGui::Begin("3D Model Viewer", p_open)) {
        ImVec2 windowSize = ImGui::GetContentRegionAvail();

        ImGui::BeginChild("ModelViewport", windowSize);

         // Handle drag and drop
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE")) {
                char* path = (char*)payload->Data;
                std::string filePath(path);
                
                // Check file extension
                std::string ext = filePath.substr(filePath.find_last_of(".") + 1);
                if (ext == "ply" || ext == "obj") {
                    loadModel(filePath);
                }
            }
            ImGui::EndDragDropTarget();
        }
        
        // Render drop zone hint if no model is loaded
        if (!modelLoaded) {
            ImVec2 textSize = ImGui::CalcTextSize("Drag and drop PLY or OBJ file here");
            ImVec2 pos(
                windowSize.x * 0.5f - textSize.x * 0.5f,
                windowSize.y * 0.5f - textSize.y * 0.5f
            );
            ImGui::SetCursorPos(pos);
            ImGui::Text("Drag and drop PLY or OBJ file here");
        }
        
        // Render the model if loaded
        if (modelLoaded) {
            // Use your OpenGL shader and render the meshes
            for (const auto& mesh : meshes) {
                glBindVertexArray(mesh.VAO);
                glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }
        }
        
        // // Check if the model is loaded
        // if (!modelLoaded) return;

        // // Use the shader program
        // glUseProgram(shaderProgram);

        // // Set the model, view, and projection matrix uniforms
        // glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        // glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        // glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

        // // Draw each mesh in the model
        // for (const auto& mesh : meshes) {
        //     // Bind the VAO of the current mesh
        //     glBindVertexArray(mesh.VAO);
            
        //     // Draw the mesh using its indices
        //     glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
            
        //     // Unbind the VAO for safety
        //     glBindVertexArray(0);
        // }

        // // Unbind the shader program after rendering
        // glUseProgram(0);
    }

    ImGui::EndChild();
    ImGui::End();
}


