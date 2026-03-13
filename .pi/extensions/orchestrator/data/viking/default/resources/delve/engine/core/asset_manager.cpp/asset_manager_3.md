ImGui::Spacing();
    ImGui::Text("Pipelines:");
    if (ImGui::BeginTable("##pipelines", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, 100))) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Vert/Shader");
        ImGui::TableSetupColumn("Frag");
        ImGui::TableSetupColumn("Rebuild?");
        ImGui::TableHeadersRow();
        for (auto &[key, rec] : pipeline_registry) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(key.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(rec.vert_shader_key.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(rec.frag_shader_key.c_str());
            ImGui::TableSetColumnIndex(3);
            if (rec.needs_rebuild)
                ImGui::TextColored({1,1,0,1}, "YES");
            else
                ImGui::TextUnformatted("no");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Tracked Buffers:");
    if (ImGui::BeginTable("##buffers", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, 80))) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Pointer");
        ImGui::TableHeadersRow();
        for (auto &[key, buf] : buffer_registry) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(key.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%p", (void*)buf);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Force Reload All Shaders")) {
        // Zero out mtimes so check_for_updates() picks them all up next frame.
        // cast away const for the button action — acceptable debug utility.
        auto *self = const_cast<AssetManager*>(this);
        for (auto &[key, asset] : self->shader_cache)
            asset.last_mtime = 0;
    }
}