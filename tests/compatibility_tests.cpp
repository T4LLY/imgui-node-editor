#include "imgui.h"
#include "imgui_node_editor.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ed = ax::NodeEditor;

namespace {

int g_failures = 0;

#define CHECK(expr)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        if (!(expr))                                                                                \
        {                                                                                           \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr << '\n';         \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

bool near(float lhs, float rhs, float epsilon = 0.01f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

bool near(const ImVec2& lhs, const ImVec2& rhs, float epsilon = 0.01f)
{
    return near(lhs.x, rhs.x, epsilon) && near(lhs.y, rhs.y, epsilon);
}

struct MemorySettings
{
    std::string global;
    std::unordered_map<std::uintptr_t, std::string> nodes;
    int begin_save_count = 0;
    int end_save_count   = 0;

    static void BeginSave(void* user_pointer)
    {
        ++static_cast<MemorySettings*>(user_pointer)->begin_save_count;
    }

    static void EndSave(void* user_pointer)
    {
        ++static_cast<MemorySettings*>(user_pointer)->end_save_count;
    }

    static bool SaveSettings(const char* data, size_t size, ed::SaveReasonFlags, void* user_pointer)
    {
        auto& self  = *static_cast<MemorySettings*>(user_pointer);
        self.global = std::string(data, size);
        return true;
    }

    static size_t LoadSettings(char* data, void* user_pointer)
    {
        const auto& value = static_cast<MemorySettings*>(user_pointer)->global;
        if (data && !value.empty())
            std::memcpy(data, value.data(), value.size());
        return value.size();
    }

    static bool SaveNodeSettings(
        ed::NodeId node_id,
        const char* data,
        size_t size,
        ed::SaveReasonFlags,
        void* user_pointer)
    {
        auto& self                 = *static_cast<MemorySettings*>(user_pointer);
        self.nodes[node_id.Get()] = std::string(data, size);
        return true;
    }

    static size_t LoadNodeSettings(ed::NodeId node_id, char* data, void* user_pointer)
    {
        const auto& self = *static_cast<MemorySettings*>(user_pointer);
        const auto it    = self.nodes.find(node_id.Get());
        if (it == self.nodes.end())
            return 0;
        if (data && !it->second.empty())
            std::memcpy(data, it->second.data(), it->second.size());
        return it->second.size();
    }
};

class Fixture
{
public:
    explicit Fixture(MemorySettings* memory = nullptr)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io       = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024.0f, 768.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();

        ed::Config config;
        config.SettingsFile = nullptr;
        if (memory)
        {
            config.UserPointer         = memory;
            config.BeginSaveSession    = &MemorySettings::BeginSave;
            config.EndSaveSession      = &MemorySettings::EndSave;
            config.SaveSettings        = &MemorySettings::SaveSettings;
            config.LoadSettings        = &MemorySettings::LoadSettings;
            config.SaveNodeSettings    = &MemorySettings::SaveNodeSettings;
            config.LoadNodeSettings    = &MemorySettings::LoadNodeSettings;
        }
        m_editor = ed::CreateEditor(&config);
        ed::SetCurrentEditor(m_editor);
    }

    ~Fixture()
    {
        ed::SetCurrentEditor(m_editor);
        ed::DestroyEditor(m_editor);
        ed::SetCurrentEditor(nullptr);
        ImGui::DestroyContext();
    }

    void frame(const std::function<void()>& contents)
    {
        auto& io       = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024.0f, 768.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.MousePos    = ImVec2(-1000.0f, -1000.0f);

        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin(
            "CompatibilityHost",
            nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ed::SetCurrentEditor(m_editor);
        ed::Begin("CompatibilityEditor", ImVec2(1000.0f, 720.0f));
        contents();
        ed::End();

        ImGui::End();
        ImGui::EndFrame();
    }

    static void submit_two_nodes(bool include_link = true)
    {
        ed::BeginNode(ed::NodeId(1));
        ImGui::TextUnformatted("Source");
        ed::BeginPin(ed::PinId(11), ed::PinKind::Output);
        ImGui::TextUnformatted("Out");
        ed::EndPin();
        ed::EndNode();

        ed::BeginNode(ed::NodeId(2));
        ImGui::TextUnformatted("Target");
        ed::BeginPin(ed::PinId(21), ed::PinKind::Input);
        ImGui::TextUnformatted("In");
        ed::EndPin();
        ed::EndNode();

        if (include_link)
            CHECK(ed::Link(ed::LinkId(100), ed::PinId(11), ed::PinId(21)));
    }

    static void submit_group()
    {
        ed::BeginNode(ed::NodeId(3));
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(300.0f, 180.0f));
        ed::EndNode();
    }

private:
    ed::EditorContext* m_editor = nullptr;
};

void test_submission_links_and_order()
{
    Fixture fixture;
    fixture.frame([] {
        Fixture::submit_two_nodes();
        CHECK(ed::GetNodeCount() == 2);
    });

    ed::PinId start;
    ed::PinId end;
    CHECK(ed::GetLinkPins(ed::LinkId(100), &start, &end));
    CHECK(start == ed::PinId(11));
    CHECK(end == ed::PinId(21));
    CHECK(ed::HasAnyLinks(ed::NodeId(1)));
    CHECK(ed::HasAnyLinks(ed::PinId(21)));

    ed::SetNodeZPosition(ed::NodeId(1), 20.0f);
    ed::SetNodeZPosition(ed::NodeId(2), 10.0f);
    fixture.frame([] { Fixture::submit_two_nodes(); });

    ed::NodeId ordered[2];
    CHECK(ed::GetOrderedNodeIds(ordered, 2) == 2);
    CHECK(ordered[0] == ed::NodeId(2));
    CHECK(ordered[1] == ed::NodeId(1));
}

void test_selection_and_position_state()
{
    Fixture fixture;
    fixture.frame([] { Fixture::submit_two_nodes(); });

    ed::SetNodePosition(ed::NodeId(1), ImVec2(120.0f, 80.0f));
    ed::SelectNode(ed::NodeId(1));
    ed::SelectLink(ed::LinkId(100), true);
    CHECK(ed::GetSelectedObjectCount() == 2);
    CHECK(ed::IsNodeSelected(ed::NodeId(1)));
    CHECK(ed::IsLinkSelected(ed::LinkId(100)));

    fixture.frame([] { Fixture::submit_two_nodes(); });
    CHECK(near(ed::GetNodePosition(ed::NodeId(1)), ImVec2(120.0f, 80.0f)));

    ed::DeselectLink(ed::LinkId(100));
    CHECK(!ed::IsLinkSelected(ed::LinkId(100)));
    ed::ClearSelection();
    CHECK(ed::GetSelectedObjectCount() == 0);
}

void test_group_suspend_and_resume()
{
    Fixture fixture;
    fixture.frame([] {
        Fixture::submit_group();
        ed::Suspend();
        CHECK(ed::IsSuspended());
        ImGui::TextUnformatted("Overlay while editor canvas is suspended");
        ed::Resume();
        CHECK(!ed::IsSuspended());
    });

    ed::SetNodePosition(ed::NodeId(3), ImVec2(200.0f, 140.0f));
    ed::SetGroupSize(ed::NodeId(3), ImVec2(320.0f, 200.0f));
    fixture.frame([] { Fixture::submit_group(); });
    CHECK(near(ed::GetNodePosition(ed::NodeId(3)), ImVec2(200.0f, 140.0f)));
    CHECK(ed::GetNodeSize(ed::NodeId(3)).x > 0.0f);
    CHECK(ed::GetNodeSize(ed::NodeId(3)).y > 0.0f);
}

void test_navigation_and_coordinate_round_trip()
{
    Fixture fixture;
    fixture.frame([] {
        Fixture::submit_two_nodes();
        ed::SetNodePosition(ed::NodeId(1), ImVec2(-500.0f, -300.0f));
        ed::SetNodePosition(ed::NodeId(2), ImVec2(700.0f, 500.0f));
    });

    ed::NavigateToContent(0.0f);
    fixture.frame([] { Fixture::submit_two_nodes(); });
    CHECK(std::isfinite(ed::GetCurrentZoom()));
    CHECK(ed::GetCurrentZoom() > 0.0f);

    const ImVec2 canvas_point(123.0f, -45.0f);
    const ImVec2 round_trip = ed::ScreenToCanvas(ed::CanvasToScreen(canvas_point));
    CHECK(near(round_trip, canvas_point, 0.05f));

    ed::SelectNode(ed::NodeId(1));
    ed::NavigateToSelection(true, 0.0f);
    fixture.frame([] { Fixture::submit_two_nodes(); });
    CHECK(std::isfinite(ed::GetCurrentZoom()));
}

void test_programmatic_delete_flow()
{
    Fixture fixture;
    fixture.frame([] { Fixture::submit_two_nodes(); });

    CHECK(ed::DeleteLink(ed::LinkId(100)));
    CHECK(ed::DeleteNode(ed::NodeId(2)));

    // Manual deletion is promoted into the active delete action during End().
    fixture.frame([] { Fixture::submit_two_nodes(); });

    bool saw_link = false;
    bool saw_node = false;
    fixture.frame([&] {
        Fixture::submit_two_nodes();
        if (ed::BeginDelete())
        {
            ed::LinkId link;
            while (ed::QueryDeletedLink(&link))
            {
                saw_link = saw_link || link == ed::LinkId(100);
                CHECK(ed::AcceptDeletedItem());
            }

            ed::NodeId node;
            while (ed::QueryDeletedNode(&node))
            {
                saw_node = saw_node || node == ed::NodeId(2);
                CHECK(ed::AcceptDeletedItem());
            }
            ed::EndDelete();
        }
    });

    CHECK(saw_link);
    CHECK(saw_node);

    fixture.frame([] {
        ed::BeginNode(ed::NodeId(1));
        ImGui::TextUnformatted("Source");
        ed::EndNode();
        CHECK(ed::GetNodeCount() == 1);
    });
}

void test_settings_restore()
{
    MemorySettings memory;
    {
        Fixture fixture(&memory);
        fixture.frame([] { Fixture::submit_two_nodes(); });
        ed::SetNodePosition(ed::NodeId(1), ImVec2(333.0f, 222.0f));
        ed::SelectNode(ed::NodeId(1));
        fixture.frame([] { Fixture::submit_two_nodes(); });
    }

    CHECK(!memory.global.empty());
    CHECK(memory.begin_save_count > 0);
    CHECK(memory.begin_save_count == memory.end_save_count);

    {
        Fixture fixture(&memory);
        fixture.frame([] { Fixture::submit_two_nodes(); });
        CHECK(near(ed::GetNodePosition(ed::NodeId(1)), ImVec2(333.0f, 222.0f)));
        CHECK(ed::IsNodeSelected(ed::NodeId(1)));
    }
}

void test_create_api_idle_smoke()
{
    Fixture fixture;
    fixture.frame([] {
        Fixture::submit_two_nodes();
        const bool creating = ed::BeginCreate();
        CHECK(!creating);
        ed::EndCreate();
    });
}

void test_repeated_frame_liveness()
{
    Fixture fixture;
    for (int frame = 0; frame < 64; ++frame)
    {
        fixture.frame([frame] {
            Fixture::submit_two_nodes((frame % 2) == 0);
            if ((frame % 3) == 0)
                Fixture::submit_group();
        });
    }

    fixture.frame([] {
        Fixture::submit_two_nodes(false);
        CHECK(ed::GetNodeCount() == 2);
    });
}

} // namespace

int main()
{
    test_submission_links_and_order();
    test_selection_and_position_state();
    test_group_suspend_and_resume();
    test_navigation_and_coordinate_round_trip();
    test_programmatic_delete_flow();
    test_settings_restore();
    test_create_api_idle_smoke();
    test_repeated_frame_liveness();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " compatibility check(s) failed\n";
        return 1;
    }

    std::cout << "imgui-node-editor compatibility tests passed\n";
    return 0;
}
