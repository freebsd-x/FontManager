// FontManager.xapp — Font Browser & Installer for FreeBSD-X
//
// Layout (860×580):
//   Title bar: "Font Manager" + search input + Install button
//   Main row:
//     Left panel (280px): scrollable font list (family + style rows)
//     Right panel (flex): preview — sample text in selected font,
//                          size selector, weight/style toggles,
//                          font info (path / format / type)
//   Footer (40px): selected font label + Remove button (user fonts only)

#include "xkit/widget.h"
#include "xkit/application.h"
#include "xkit/xbus_client.h"
#include "xkit/file_dialog.h"
#include "shared/protocol.h"
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <functional>
#include <algorithm>

using namespace fbx::xkit;
using namespace fbx::shared;

// ── App ───────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // ── Shared state ──────────────────────────────────────────────────────────
    auto all_fonts      = std::make_shared<std::vector<FontInfo>>();
    auto filter_str     = std::make_shared<std::string>("");
    auto selected_idx   = std::make_shared<int>(-1);   // index into all_fonts
    auto preview_size   = std::make_shared<float>(24.0f);
    auto preview_weight = std::make_shared<uint16_t>(400);
    auto status_str     = std::make_shared<std::string>("Loading fonts…");

    // Deferred widget pointers
    auto font_col_ptr    = std::make_shared<std::shared_ptr<FlexColumn>>();
    auto preview_lbl_ptr = std::make_shared<std::shared_ptr<TextWidget>>();
    auto info_family_ptr = std::make_shared<std::shared_ptr<TextWidget>>();
    auto info_style_ptr  = std::make_shared<std::shared_ptr<TextWidget>>();
    auto info_path_ptr   = std::make_shared<std::shared_ptr<TextWidget>>();
    auto info_fmt_ptr    = std::make_shared<std::shared_ptr<TextWidget>>();
    auto info_type_ptr   = std::make_shared<std::shared_ptr<TextWidget>>();
    auto remove_btn_ptr  = std::make_shared<std::shared_ptr<ButtonWidget>>();
    auto status_lbl_ptr  = std::make_shared<std::shared_ptr<TextWidget>>();

    // Deferred lambdas
    auto rebuild_list    = std::make_shared<std::function<void()>>();
    auto apply_selection = std::make_shared<std::function<void()>>();
    auto refresh_fonts   = std::make_shared<std::function<void()>>();

    static XBusClient* g_xbus = nullptr;

    auto set_status = [status_lbl_ptr, status_str](const std::string& msg) {
        *status_str = msg;
        if (*status_lbl_ptr) (*status_lbl_ptr)->setText(msg);
    };

    // ── Root ─────────────────────────────────────────────────────────────────
    auto root_col = std::make_shared<FlexColumn>();
    root_col->setBackgroundColor(Color{0.10f, 0.10f, 0.10f, 1.0f});

    return XApplication::Run(root_col, "/tmp/freebsdx.sock", /*sandboxed=*/false,
                             /*is_shell=*/false,
        [&](XBusClient& xbus) {
            g_xbus = &xbus;
            XApplication::setTitle("Font Manager");

            // ── refresh_fonts ─────────────────────────────────────────────────
            *refresh_fonts = [all_fonts, font_col_ptr, rebuild_list, set_status]() {
                if (!g_xbus) return;
                all_fonts->clear();

                set_status("Loading fonts…");

                auto fetch = std::make_shared<std::function<void(uint32_t)>>();
                *fetch = [all_fonts, font_col_ptr, rebuild_list, set_status, fetch](uint32_t page) {
                    FontPageRequest req{};
                    req.page_ = page;
                    g_xbus->Call("com.freebsdx.FontManager",
                        static_cast<uint32_t>(FontManagerOpcode::ListFonts),
                        {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                        [all_fonts, font_col_ptr, rebuild_list, set_status, fetch, page](
                                const XBusMessage& r) {
                            FontListPage pg{};
                            std::memcpy(&pg, r.payload_, sizeof(pg));

                            if (pg.count_ > 0) {
                                pg.font_.family_[sizeof(pg.font_.family_)-1] = '\0';
                                pg.font_.style_[sizeof(pg.font_.style_)-1]   = '\0';
                                pg.font_.path_[sizeof(pg.font_.path_)-1]     = '\0';
                                pg.font_.format_[sizeof(pg.font_.format_)-1] = '\0';
                                all_fonts->push_back(pg.font_);
                            }

                            if (pg.has_more_) {
                                if (*fetch) (*fetch)(page + 1);
                            } else {
                                char buf[48];
                                std::snprintf(buf, sizeof(buf), "%zu fonts installed",
                                              all_fonts->size());
                                set_status(buf);
                                if (*rebuild_list) (*rebuild_list)();
                            }
                        });
                };
                (*fetch)(0);
            };

            // ── apply_selection ───────────────────────────────────────────────
            *apply_selection = [all_fonts, selected_idx, preview_size, preview_weight,
                                preview_lbl_ptr, info_family_ptr, info_style_ptr,
                                info_path_ptr, info_fmt_ptr, info_type_ptr,
                                remove_btn_ptr]() {
                int idx = *selected_idx;
                if (idx < 0 || idx >= static_cast<int>(all_fonts->size())) return;

                const FontInfo& fi = (*all_fonts)[idx];

                // Update preview TextWidget
                if (*preview_lbl_ptr) {
                    auto* lbl = (*preview_lbl_ptr).get();
                    lbl->setFontFamily(std::string(fi.family_));
                    lbl->setFontSize(*preview_size);
                    lbl->setFontWeight(*preview_weight);
                }

                // Update info labels
                if (*info_family_ptr) (*info_family_ptr)->setText(
                    std::string("Family: ") + fi.family_);
                if (*info_style_ptr)  (*info_style_ptr)->setText(
                    std::string("Style: ") + fi.style_);
                if (*info_path_ptr)   (*info_path_ptr)->setText(
                    std::string("Path: ") + fi.path_);
                if (*info_fmt_ptr)    (*info_fmt_ptr)->setText(
                    std::string("Format: ") + fi.format_);
                if (*info_type_ptr)   (*info_type_ptr)->setText(
                    fi.is_user_installed_ ? "User-installed" : "System font");

                // Enable Remove only for user fonts
                if (*remove_btn_ptr) {
                    bool can_remove = fi.is_user_installed_ != 0;
                    (*remove_btn_ptr)->setBackgroundColor(
                        can_remove ? Color{0.55f, 0.18f, 0.18f, 1.0f}
                                   : Color{0.22f, 0.22f, 0.24f, 1.0f});
                }
            };

            // ── rebuild_list ──────────────────────────────────────────────────
            *rebuild_list = [all_fonts, filter_str, selected_idx,
                             font_col_ptr, apply_selection]() {
                auto col = *font_col_ptr;
                if (!col) return;
                col->clearChildren();

                const std::string& fl = *filter_str;
                auto toLower = [](std::string s) {
                    for (auto& c : s)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return s;
                };
                std::string fll = toLower(fl);

                int cur_sel = *selected_idx;

                for (int i = 0; i < static_cast<int>(all_fonts->size()); ++i) {
                    const FontInfo& fi = (*all_fonts)[i];
                    std::string fam = fi.family_;
                    std::string sty = fi.style_;

                    if (!fll.empty()) {
                        if (toLower(fam).find(fll) == std::string::npos &&
                            toLower(sty).find(fll) == std::string::npos)
                            continue;
                    }

                    bool is_sel = (i == cur_sel);
                    bool is_user = fi.is_user_installed_ != 0;

                    auto fam_lbl = std::make_shared<TextWidget>(fam, 13.0f);
                    fam_lbl->setColor(Color{0.92f, 0.92f, 0.95f, 1.0f});

                    auto sty_lbl = std::make_shared<TextWidget>(sty, 11.0f);
                    sty_lbl->setColor(
                        is_user ? Color{0.50f, 0.80f, 0.55f, 1.0f}
                                : Color{0.50f, 0.50f, 0.55f, 1.0f});

                    auto entry_col = std::make_shared<FlexColumn>(
                        std::vector<std::shared_ptr<Widget>>{ fam_lbl, sty_lbl });
                    entry_col->setGap(2.0f);

                    auto entry_btn = std::make_shared<ButtonWidget>(
                        entry_col,
                        [i, selected_idx, font_col_ptr, apply_selection]() {
                            *selected_idx = i;
                            if (*apply_selection) (*apply_selection)();
                            // Update selection highlight
                            auto col2 = *font_col_ptr;
                            if (!col2) return;
                            for (size_t j = 0; j < col2->childCount(); ++j) {
                                auto btn = std::dynamic_pointer_cast<ButtonWidget>(col2->childAt(j));
                                if (btn) {
                                    btn->setBackgroundColor(
                                        (static_cast<int>(j) == i)
                                            ? Color{0.18f, 0.28f, 0.50f, 1.0f}
                                            : Color{0.13f, 0.13f, 0.15f, 1.0f});
                                }
                            }
                        });
                    entry_btn->setPadding(8.0f);
                    entry_btn->setFixedSize(0.0f, 50.0f);
                    entry_btn->setBackgroundColor(
                        is_sel ? Color{0.18f, 0.28f, 0.50f, 1.0f}
                               : Color{0.13f, 0.13f, 0.15f, 1.0f});

                    col->addChild(entry_btn);
                }
            };

            // ── Title / search bar ────────────────────────────────────────────
            auto title_lbl = std::make_shared<TextWidget>("Font Manager", 16.0f);
            title_lbl->setColor(Color{0.95f, 0.95f, 0.95f, 1.0f});

            auto search_input = std::make_shared<TextInputWidget>("");
            search_input->setPlaceholder("Search fonts…");
            search_input->setFlexGrow(1.0f);
            search_input->setFixedSize(0.0f, 28.0f);
            search_input->setOnChanged([filter_str, rebuild_list](const std::string& txt) {
                *filter_str = txt;
                if (*rebuild_list) (*rebuild_list)();
            });

            auto install_btn = std::make_shared<ButtonWidget>("+ Install Font",
                [set_status, refresh_fonts]() {
                    if (!g_xbus) return;
                    XFileDialog::openFile(
                        *g_xbus,
                        "Select font file",
                        "font/*",
                        [set_status, refresh_fonts](const std::string& path) {
                            if (path.empty()) return;
                            FontInstallRequest req{};
                            std::strncpy(req.path_, path.c_str(), sizeof(req.path_) - 1);
                            g_xbus->Call("com.freebsdx.FontManager",
                                static_cast<uint32_t>(FontManagerOpcode::InstallFont),
                                {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                                [set_status, refresh_fonts](const XBusMessage& r) {
                                    FontStatusResponse resp{};
                                    std::memcpy(&resp, r.payload_, sizeof(resp));
                                    resp.message_[sizeof(resp.message_)-1] = '\0';
                                    set_status(resp.message_);
                                    // FontsChanged broadcast will trigger refresh
                                });
                        });
                });
            install_btn->setFontSize(12.0f);
            install_btn->setFixedSize(120.0f, 28.0f);
            install_btn->setBackgroundColor(Color{0.20f, 0.38f, 0.22f, 1.0f});

            auto title_row = std::make_shared<FlexRow>(
                std::vector<std::shared_ptr<Widget>>{ title_lbl, search_input, install_btn });
            title_row->setCrossAxisAlignment(CrossAxisAlignment::Center);
            title_row->setFixedSize(0.0f, 44.0f);
            title_row->setPadding(10.0f);
            title_row->setGap(10.0f);
            title_row->setBackgroundColor(Color{0.12f, 0.12f, 0.14f, 1.0f});

            // ── Left panel — font list ────────────────────────────────────────
            auto font_col = std::make_shared<FlexColumn>();
            font_col->setGap(1.0f);
            *font_col_ptr = font_col;

            auto font_scroll = std::make_shared<ScrollViewWidget>(font_col);
            font_scroll->setFlexGrow(1.0f);

            auto left_hdr = std::make_shared<TextWidget>("INSTALLED FONTS", 11.0f);
            left_hdr->setColor(Color{0.50f, 0.55f, 0.65f, 1.0f});
            left_hdr->setPadding(6.0f);

            auto left_hdr_row = std::make_shared<FlexRow>(
                std::vector<std::shared_ptr<Widget>>{ left_hdr });
            left_hdr_row->setFixedSize(0.0f, 26.0f);
            left_hdr_row->setCrossAxisAlignment(CrossAxisAlignment::Center);
            left_hdr_row->setBackgroundColor(Color{0.14f, 0.14f, 0.16f, 1.0f});

            auto left_panel = std::make_shared<FlexColumn>(
                std::vector<std::shared_ptr<Widget>>{ left_hdr_row, font_scroll });
            left_panel->setFixedSize(280.0f, 0.0f);
            left_panel->setBackgroundColor(Color{0.12f, 0.12f, 0.14f, 1.0f});

            // ── Right panel — preview + info ──────────────────────────────────

            // Sample text input
            auto sample_input = std::make_shared<TextInputWidget>(
                "The quick brown fox jumps over the lazy dog. 0123456789");
            sample_input->setFixedSize(0.0f, 32.0f);
            sample_input->setOnChanged([preview_lbl_ptr](const std::string& txt) {
                if (*preview_lbl_ptr) (*preview_lbl_ptr)->setText(txt);
            });

            // Size selector buttons
            auto make_size_btn = [preview_size, preview_lbl_ptr, apply_selection](float sz) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%g", sz);
                auto btn = std::make_shared<ButtonWidget>(std::string(buf) + "pt",
                    [sz, preview_size, preview_lbl_ptr, apply_selection]() {
                        *preview_size = sz;
                        if (*preview_lbl_ptr) (*preview_lbl_ptr)->setFontSize(sz);
                    });
                btn->setFontSize(11.0f);
                btn->setFixedSize(44.0f, 26.0f);
                btn->setBackgroundColor(Color{0.22f, 0.22f, 0.25f, 1.0f});
                return btn;
            };

            auto sizes_row = std::make_shared<FlexRow>(
                std::vector<std::shared_ptr<Widget>>{
                    make_size_btn(12.0f), make_size_btn(16.0f),
                    make_size_btn(24.0f), make_size_btn(32.0f),
                    make_size_btn(48.0f), make_size_btn(64.0f),
                    std::make_shared<Spacer>(0.0f, 1.0f, true) });
            sizes_row->setCrossAxisAlignment(CrossAxisAlignment::Center);
            sizes_row->setFixedSize(0.0f, 36.0f);
            sizes_row->setPadding(4.0f);
            sizes_row->setGap(4.0f);
            sizes_row->setBackgroundColor(Color{0.13f, 0.13f, 0.15f, 1.0f});

            // Weight buttons
            auto make_weight_btn = [preview_weight, preview_lbl_ptr](uint16_t w, const char* label) {
                auto btn = std::make_shared<ButtonWidget>(std::string(label),
                    [w, preview_weight, preview_lbl_ptr]() {
                        *preview_weight = w;
                        if (*preview_lbl_ptr) (*preview_lbl_ptr)->setFontWeight(w);
                    });
                btn->setFontSize(11.0f);
                btn->setFixedSize(60.0f, 26.0f);
                btn->setBackgroundColor(Color{0.22f, 0.22f, 0.25f, 1.0f});
                return btn;
            };

            auto weights_row = std::make_shared<FlexRow>(
                std::vector<std::shared_ptr<Widget>>{
                    make_weight_btn(300, "Light"),
                    make_weight_btn(400, "Regular"),
                    make_weight_btn(600, "SemiBold"),
                    make_weight_btn(700, "Bold"),
                    make_weight_btn(900, "Black"),
                    std::make_shared<Spacer>(0.0f, 1.0f, true) });
            weights_row->setCrossAxisAlignment(CrossAxisAlignment::Center);
            weights_row->setFixedSize(0.0f, 36.0f);
            weights_row->setPadding(4.0f);
            weights_row->setGap(4.0f);
            weights_row->setBackgroundColor(Color{0.12f, 0.12f, 0.14f, 1.0f});

            // Preview text
            auto preview_lbl = std::make_shared<TextWidget>(
                "The quick brown fox jumps over the lazy dog. 0123456789", 24.0f);
            preview_lbl->setColor(Color{0.92f, 0.92f, 0.95f, 1.0f});
            preview_lbl->setPadding(16.0f);
            preview_lbl->setFlexGrow(1.0f);
            *preview_lbl_ptr = preview_lbl;

            auto preview_scroll = std::make_shared<ScrollViewWidget>(preview_lbl);
            preview_scroll->setFlexGrow(1.0f);

            auto preview_card = std::make_shared<FlexColumn>(
                std::vector<std::shared_ptr<Widget>>{ preview_scroll });
            preview_card->setFlexGrow(1.0f);
            preview_card->setBackgroundColor(Color{0.08f, 0.08f, 0.09f, 1.0f});

            // Font info section
            auto make_info_lbl = []() {
                auto lbl = std::make_shared<TextWidget>("", 11.0f);
                lbl->setColor(Color{0.55f, 0.55f, 0.58f, 1.0f});
                lbl->setPadding(2.0f);
                return lbl;
            };

            auto info_family = make_info_lbl();
            auto info_style  = make_info_lbl();
            auto info_path   = make_info_lbl();
            auto info_fmt    = make_info_lbl();
            auto info_type   = make_info_lbl();
            *info_family_ptr = info_family;
            *info_style_ptr  = info_style;
            *info_path_ptr   = info_path;
            *info_fmt_ptr    = info_fmt;
            *info_type_ptr   = info_type;

            auto info_col = std::make_shared<FlexColumn>(
                std::vector<std::shared_ptr<Widget>>{
                    info_family, info_style, info_path, info_fmt, info_type });
            info_col->setGap(2.0f);
            info_col->setPadding(8.0f);
            info_col->setFixedSize(0.0f, 80.0f);
            info_col->setBackgroundColor(Color{0.13f, 0.13f, 0.15f, 1.0f});

            // Right panel assembly
            auto right_panel = std::make_shared<FlexColumn>(
                std::vector<std::shared_ptr<Widget>>{
                    sample_input,
                    sizes_row,
                    weights_row,
                    std::make_shared<DividerWidget>(0.5f),
                    preview_card,
                    std::make_shared<DividerWidget>(0.5f),
                    info_col });
            right_panel->setFlexGrow(1.0f);
            right_panel->setBackgroundColor(Color{0.10f, 0.10f, 0.12f, 1.0f});

            // ── Main content row ──────────────────────────────────────────────
            auto main_row = std::make_shared<FlexRow>(
                std::vector<std::shared_ptr<Widget>>{
                    left_panel,
                    std::make_shared<DividerWidget>(1.0f),
                    right_panel });
            main_row->setFlexGrow(1.0f);

            // ── Footer ────────────────────────────────────────────────────────
            auto status_lbl = std::make_shared<TextWidget>("Ready", 12.0f);
            status_lbl->setColor(Color{0.55f, 0.55f, 0.58f, 1.0f});
            status_lbl->setFlexGrow(1.0f);
            *status_lbl_ptr = status_lbl;
            set_status("Loading fonts…");

            auto remove_btn = std::make_shared<ButtonWidget>("Remove Font",
                [selected_idx, all_fonts, set_status, refresh_fonts]() {
                    int idx = *selected_idx;
                    if (idx < 0 || idx >= static_cast<int>(all_fonts->size())) {
                        set_status("Select a user-installed font to remove.");
                        return;
                    }
                    const FontInfo& fi = (*all_fonts)[idx];
                    if (!fi.is_user_installed_) {
                        set_status("System fonts cannot be removed.");
                        return;
                    }
                    if (!g_xbus) return;
                    FontRemoveRequest req{};
                    std::strncpy(req.path_, fi.path_, sizeof(req.path_) - 1);
                    g_xbus->Call("com.freebsdx.FontManager",
                        static_cast<uint32_t>(FontManagerOpcode::RemoveFont),
                        {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                        [set_status](const XBusMessage& r) {
                            FontStatusResponse resp{};
                            std::memcpy(&resp, r.payload_, sizeof(resp));
                            resp.message_[sizeof(resp.message_)-1] = '\0';
                            set_status(resp.message_);
                            // FontsChanged broadcast triggers refresh
                        });
                });
            remove_btn->setFontSize(12.0f);
            remove_btn->setFixedSize(110.0f, 28.0f);
            remove_btn->setBackgroundColor(Color{0.22f, 0.22f, 0.24f, 1.0f}); // disabled look
            *remove_btn_ptr = remove_btn;

            auto footer_row = std::make_shared<FlexRow>(
                std::vector<std::shared_ptr<Widget>>{ status_lbl, remove_btn });
            footer_row->setCrossAxisAlignment(CrossAxisAlignment::Center);
            footer_row->setFixedSize(0.0f, 40.0f);
            footer_row->setPadding(8.0f);
            footer_row->setGap(8.0f);
            footer_row->setBackgroundColor(Color{0.12f, 0.12f, 0.14f, 1.0f});

            // ── Assemble root ─────────────────────────────────────────────────
            root_col->addChild(title_row);
            root_col->addChild(std::make_shared<DividerWidget>(1.0f));
            root_col->addChild(main_row);
            root_col->addChild(std::make_shared<DividerWidget>(1.0f));
            root_col->addChild(footer_row);

            // ── Initial font load ─────────────────────────────────────────────
            if (*refresh_fonts) (*refresh_fonts)();

            // ── XBus broadcast: FontsChanged ──────────────────────────────────
            xbus.OnBroadcast([refresh_fonts, selected_idx](const XBusMessage& msg) {
                if (std::strcmp(msg.service_name_, "com.freebsdx.FontManager") == 0 &&
                    msg.opcode_ == static_cast<uint32_t>(FontManagerOpcode::FontsChanged)) {
                    *selected_idx = -1;
                    if (*refresh_fonts) (*refresh_fonts)();
                }
            });
        });
}
