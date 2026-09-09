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
    int begin_save_count    = 0;
    int end_save_count      = 0;
    int save_settings_count = 0;
    bool save_settings_result = true;

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
        ++self.save_settings_count;
        self.global = std::string(data, size);
        return self.save_settings_result;
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

    explicit Fixture(const ed::Config& config)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io       = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024.0f, 768.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();

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
        frame_at(ImVec2(-1000.0f, -1000.0f), contents);
    }

    void frame_at(const ImVec2& mouse_pos, const std::function<void()>& contents)
    {
        auto& io       = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024.0f, 768.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.MousePos    = mouse_pos;

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

        // The node editor uses an internal ImDrawCallback sentinel while
        // transforming canvas draw commands. It must never escape ed::End(),
        // otherwise render backends may try to invoke it as a function pointer.
        const auto* draw_list = ImGui::GetWindowDrawList();
        for (int i = 0; i < draw_list->CmdBuffer.Size; ++i)
            CHECK(draw_list->CmdBuffer[i].UserCallback == nullptr);

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

void test_disabled_persistence_skips_save_sessions()
{
    MemorySettings memory;
    ed::Config config;
    config.SettingsFile     = nullptr;
    config.UserPointer      = &memory;
    config.BeginSaveSession = &MemorySettings::BeginSave;
    config.EndSaveSession   = &MemorySettings::EndSave;

    {
        Fixture fixture(config);
        fixture.frame([] { Fixture::submit_two_nodes(); });
        fixture.frame([] { Fixture::submit_two_nodes(); });
    }

    CHECK(memory.begin_save_count == 0);
    CHECK(memory.end_save_count == 0);
    CHECK(memory.save_settings_count == 0);
}

void test_failed_settings_save_is_retried()
{
    MemorySettings memory;
    memory.save_settings_result = false;

    {
        Fixture fixture(&memory);
        fixture.frame([] { Fixture::submit_two_nodes(); });
        const int first_save_count = memory.save_settings_count;
        CHECK(first_save_count > 0);

        fixture.frame([] { Fixture::submit_two_nodes(); });
        CHECK(memory.save_settings_count > first_save_count);
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

void test_virtual_node_submission_keeps_links_and_geometry_alive()
{
    Fixture fixture;
    fixture.frame([] { Fixture::submit_two_nodes(); });

    ed::SetNodePosition(ed::NodeId(1), ImVec2(120.0f, 100.0f));
    ed::SetNodePosition(ed::NodeId(2), ImVec2(520.0f, 260.0f));

    const ed::VirtualPinDesc source_pin = {
        ed::PinId(11),
        ed::PinKind::Output,
        ImVec2(110.0f, 28.0f),
        ImVec2(126.0f, 44.0f),
        ImVec2(118.0f, 32.0f),
        ImVec2(126.0f, 40.0f),
    };
    const ed::VirtualPinDesc target_pin = {
        ed::PinId(21),
        ed::PinKind::Input,
        ImVec2(-6.0f, 28.0f),
        ImVec2(10.0f, 44.0f),
        ImVec2(-6.0f, 32.0f),
        ImVec2(2.0f, 40.0f),
    };

    const ed::VirtualNodeDesc source = {
        ed::NodeId(1), ImVec2(120.0f, 72.0f), &source_pin, 1,
    };
    const ed::VirtualNodeDesc target = {
        ed::NodeId(2), ImVec2(120.0f, 72.0f), &target_pin, 1,
    };

    fixture.frame([&] {
        CHECK(ed::SubmitVirtualNode(source));
        CHECK(ed::SubmitVirtualNode(target));
        CHECK(ed::Link(ed::LinkId(100), ed::PinId(11), ed::PinId(21)));
        CHECK(ed::GetNodeCount() == 2);
        CHECK(ed::GetNodeBackgroundDrawList(ed::NodeId(1)) == nullptr);
        CHECK(ed::GetNodeBackgroundDrawList(ed::NodeId(2)) == nullptr);

        ImVec2 visible_min;
        ImVec2 visible_max;
        ed::GetVisibleCanvasBounds(&visible_min, &visible_max);
        CHECK(visible_min.x < visible_max.x);
        CHECK(visible_min.y < visible_max.y);
        CHECK(ed::IsNodeVisible(ed::NodeId(1)));
    });

    CHECK(near(ed::GetNodePosition(ed::NodeId(1)), ImVec2(120.0f, 100.0f)));
    CHECK(near(ed::GetNodeSize(ed::NodeId(1)), ImVec2(120.0f, 72.0f)));

    ed::PinId start;
    ed::PinId end;
    CHECK(ed::GetLinkPins(ed::LinkId(100), &start, &end));
    CHECK(start == ed::PinId(11));
    CHECK(end == ed::PinId(21));

    fixture.frame([] {
        Fixture::submit_two_nodes();
        CHECK(ed::GetNodeBackgroundDrawList(ed::NodeId(1)) != nullptr);
    });
}

void test_virtual_node_validation_is_transactional()
{
    Fixture fixture;

    fixture.frame([] {
        const ed::VirtualNodeDesc negative_size = {
            ed::NodeId(50), ImVec2(-1.0f, 20.0f), nullptr, 0,
        };
        CHECK(!ed::SubmitVirtualNode(negative_size));
        CHECK(ed::GetNodeCount() == 0);

        const ed::VirtualPinDesc duplicate_pins[] = {
            {ed::PinId(501), ed::PinKind::Input, ImVec2(0, 0), ImVec2(10, 10), ImVec2(0, 0), ImVec2(4, 4)},
            {ed::PinId(501), ed::PinKind::Output, ImVec2(10, 0), ImVec2(20, 10), ImVec2(16, 0), ImVec2(20, 4)},
        };
        const ed::VirtualNodeDesc duplicate = {
            ed::NodeId(51), ImVec2(100.0f, 40.0f), duplicate_pins, 2,
        };
        CHECK(!ed::SubmitVirtualNode(duplicate));
        CHECK(ed::GetNodeCount() == 0);

        const ed::VirtualNodeDesc valid = {
            ed::NodeId(52), ImVec2(100.0f, 40.0f), nullptr, 0,
        };
        CHECK(ed::SubmitVirtualNode(valid));
        CHECK(!ed::SubmitVirtualNode(valid));
        CHECK(ed::GetNodeCount() == 1);
    });
}

void test_virtual_pin_interaction_uses_retained_bounds()
{
    Fixture fixture;
    fixture.frame([] {});

    ed::SetNodePosition(ed::NodeId(70), ImVec2(200.0f, 150.0f));

    const ed::VirtualPinDesc pin = {
        ed::PinId(701),
        ed::PinKind::Output,
        ImVec2(110.0f, 20.0f),
        ImVec2(130.0f, 40.0f),
        ImVec2(118.0f, 24.0f),
        ImVec2(130.0f, 36.0f),
    };
    const ed::VirtualNodeDesc node = {
        ed::NodeId(70), ImVec2(100.0f, 60.0f), &pin, 1,
    };

    fixture.frame_at(ImVec2(10.0f, 10.0f), [&] {
        ImGui::GetIO().MousePos = ImVec2(320.0f, 180.0f);
        CHECK(ed::SubmitVirtualNode(node));
    });

    CHECK(ed::GetHoveredNode() == ed::NodeId(70));
    CHECK(ed::GetHoveredPin() == ed::PinId(701));
}

void test_retained_pin_geometry_translates_with_node()
{
    Fixture fixture;
    fixture.frame([] {});

    ed::SetNodePosition(ed::NodeId(80), ImVec2(100.0f, 90.0f));
    const ed::VirtualPinDesc pin = {
        ed::PinId(801),
        ed::PinKind::Input,
        ImVec2(-8.0f, 18.0f),
        ImVec2(8.0f, 34.0f),
        ImVec2(-8.0f, 22.0f),
        ImVec2(0.0f, 30.0f),
    };
    const ed::VirtualNodeDesc node = {
        ed::NodeId(80), ImVec2(120.0f, 64.0f), &pin, 1,
    };

    const ImVec2 moved_position(260.0f, 210.0f);
    fixture.frame_at(ImVec2(10.0f, 10.0f), [&] {
        CHECK(ed::SubmitVirtualNode(node));
        ed::SetNodePosition(ed::NodeId(80), moved_position);
        ImGui::GetIO().MousePos = ImVec2(moved_position.x, moved_position.y + 26.0f);
    });

    CHECK(ed::GetHoveredNode() == ed::NodeId(80));
    CHECK(ed::GetHoveredPin() == ed::PinId(801));
}

void test_visible_link_candidates_keep_cross_view_links_interactive()
{
    Fixture fixture;
    fixture.frame([] {});

    fixture.frame_at(ImVec2(10.0f, 10.0f), [] {
        const ed::VirtualPinDesc source_pin = {
            ed::PinId(9011), ed::PinKind::Output,
            ImVec2(110.0f, 22.0f), ImVec2(126.0f, 38.0f),
            ImVec2(118.0f, 26.0f), ImVec2(126.0f, 34.0f),
        };
        const ed::VirtualPinDesc target_pin = {
            ed::PinId(9021), ed::PinKind::Input,
            ImVec2(-6.0f, 22.0f), ImVec2(10.0f, 38.0f),
            ImVec2(-6.0f, 26.0f), ImVec2(2.0f, 34.0f),
        };

        ed::SetNodePosition(ed::NodeId(901), ImVec2(-260.0f, 220.0f));
        ed::SetNodePosition(ed::NodeId(902), ImVec2(1120.0f, 220.0f));
        const ed::VirtualNodeDesc source = {ed::NodeId(901), ImVec2(120.0f, 60.0f), &source_pin, 1};
        const ed::VirtualNodeDesc target = {ed::NodeId(902), ImVec2(120.0f, 60.0f), &target_pin, 1};
        CHECK(ed::SubmitVirtualNode(source));
        CHECK(ed::SubmitVirtualNode(target));
        CHECK(ed::Link(ed::LinkId(900), ed::PinId(9011), ed::PinId(9021)));

        // Populate many links that are entirely outside the clip rect. They
        // must not interfere with hit testing of the link crossing the view.
        for (int i = 0; i < 128; ++i)
        {
            const auto source_node = ed::NodeId(1000 + i * 2);
            const auto target_node = ed::NodeId(1001 + i * 2);
            const auto source_pin_id = ed::PinId(10000 + i * 2);
            const auto target_pin_id = ed::PinId(10001 + i * 2);
            const auto link_id = ed::LinkId(10000 + i);
            const float y = 5000.0f + static_cast<float>(i) * 80.0f;

            ed::SetNodePosition(source_node, ImVec2(100.0f, y));
            ed::SetNodePosition(target_node, ImVec2(420.0f, y));

            const ed::VirtualPinDesc off_source_pin = {
                source_pin_id, ed::PinKind::Output,
                ImVec2(110.0f, 22.0f), ImVec2(126.0f, 38.0f),
                ImVec2(118.0f, 26.0f), ImVec2(126.0f, 34.0f),
            };
            const ed::VirtualPinDesc off_target_pin = {
                target_pin_id, ed::PinKind::Input,
                ImVec2(-6.0f, 22.0f), ImVec2(10.0f, 38.0f),
                ImVec2(-6.0f, 26.0f), ImVec2(2.0f, 34.0f),
            };
            const ed::VirtualNodeDesc off_source = {source_node, ImVec2(120.0f, 60.0f), &off_source_pin, 1};
            const ed::VirtualNodeDesc off_target = {target_node, ImVec2(120.0f, 60.0f), &off_target_pin, 1};
            CHECK(ed::SubmitVirtualNode(off_source));
            CHECK(ed::SubmitVirtualNode(off_target));
            CHECK(ed::Link(link_id, source_pin_id, target_pin_id));
        }

        ImGui::GetIO().MousePos = ImVec2(500.0f, 250.0f);
    });

    CHECK(ed::GetHoveredLink() == ed::LinkId(900));
}


void test_large_virtual_graph_uses_retained_spatial_candidates()
{
    Fixture fixture;
    fixture.frame([] {});

    constexpr int node_count = 2048;
    const ed::NodeId target_id(20000 + node_count - 1);
    const ImVec2 target_position(420.0f, 300.0f);

    fixture.frame_at(ImVec2(10.0f, 10.0f), [&] {
        for (int i = 0; i < node_count; ++i)
        {
            const ed::NodeId id(20000 + i);
            const ImVec2 position = i == node_count - 1
                ? target_position
                : ImVec2(
                    5000.0f + static_cast<float>(i % 64) * 180.0f,
                    5000.0f + static_cast<float>(i / 64) * 100.0f);

            ed::SetNodePosition(id, position);
            const ed::VirtualNodeDesc node = {id, ImVec2(140.0f, 64.0f), nullptr, 0};
            CHECK(ed::SubmitVirtualNode(node));
        }

        ImGui::GetIO().MousePos = ImVec2(target_position.x + 40.0f, target_position.y + 24.0f);
    });

    CHECK(ed::GetHoveredNode() == target_id);
    CHECK(near(ed::GetNodePosition(target_id), target_position));

    ed::SelectNode(target_id);
    CHECK(ed::IsNodeSelected(target_id));
}

void test_link_adjacency_tracks_rebound_link_endpoints()
{
    Fixture fixture;
    fixture.frame([] {});

    fixture.frame([] {
        const ed::VirtualPinDesc source_pin = {
            ed::PinId(31001), ed::PinKind::Output,
            ImVec2(110.0f, 20.0f), ImVec2(126.0f, 36.0f),
            ImVec2(118.0f, 24.0f), ImVec2(126.0f, 32.0f),
        };
        const ed::VirtualPinDesc target_a_pin = {
            ed::PinId(31002), ed::PinKind::Input,
            ImVec2(-6.0f, 20.0f), ImVec2(10.0f, 36.0f),
            ImVec2(-6.0f, 24.0f), ImVec2(2.0f, 32.0f),
        };
        const ed::VirtualPinDesc target_b_pin = {
            ed::PinId(31003), ed::PinKind::Input,
            ImVec2(-6.0f, 20.0f), ImVec2(10.0f, 36.0f),
            ImVec2(-6.0f, 24.0f), ImVec2(2.0f, 32.0f),
        };

        ed::SetNodePosition(ed::NodeId(31101), ImVec2(100.0f, 160.0f));
        ed::SetNodePosition(ed::NodeId(31102), ImVec2(420.0f, 120.0f));
        ed::SetNodePosition(ed::NodeId(31103), ImVec2(420.0f, 260.0f));

        const ed::VirtualNodeDesc source = {ed::NodeId(31101), ImVec2(120.0f, 60.0f), &source_pin, 1};
        const ed::VirtualNodeDesc target_a = {ed::NodeId(31102), ImVec2(120.0f, 60.0f), &target_a_pin, 1};
        const ed::VirtualNodeDesc target_b = {ed::NodeId(31103), ImVec2(120.0f, 60.0f), &target_b_pin, 1};
        CHECK(ed::SubmitVirtualNode(source));
        CHECK(ed::SubmitVirtualNode(target_a));
        CHECK(ed::SubmitVirtualNode(target_b));

        CHECK(ed::Link(ed::LinkId(31999), ed::PinId(31001), ed::PinId(31002)));
        CHECK(ed::HasAnyLinks(ed::NodeId(31102)));
        CHECK(ed::HasAnyLinks(ed::PinId(31002)));

        // Re-submitting an existing link ID with a different endpoint must
        // update the adjacency cache transactionally in the same frame.
        CHECK(ed::Link(ed::LinkId(31999), ed::PinId(31001), ed::PinId(31003)));
        CHECK(!ed::HasAnyLinks(ed::NodeId(31102)));
        CHECK(!ed::HasAnyLinks(ed::PinId(31002)));
        CHECK(ed::HasAnyLinks(ed::NodeId(31103)));
        CHECK(ed::HasAnyLinks(ed::PinId(31003)));
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
    test_disabled_persistence_skips_save_sessions();
    test_failed_settings_save_is_retried();
    test_create_api_idle_smoke();
    test_repeated_frame_liveness();
    test_virtual_node_submission_keeps_links_and_geometry_alive();
    test_virtual_node_validation_is_transactional();
    test_virtual_pin_interaction_uses_retained_bounds();
    test_retained_pin_geometry_translates_with_node();
    test_visible_link_candidates_keep_cross_view_links_interactive();
    test_large_virtual_graph_uses_retained_spatial_candidates();
    test_link_adjacency_tracks_rebound_link_endpoints();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " compatibility check(s) failed\n";
        return 1;
    }

    std::cout << "imgui-node-editor compatibility tests passed\n";
    return 0;
}
