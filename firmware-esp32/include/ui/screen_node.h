// include/ui/screen_node.h — Node & Network screen: node name + verification
// badge + uptime as headline stats, and the mempool.space-style mining
// animation (mined blocks sliding left through a dashed divider, pending
// block shown as a dissolving countdown ring). Mirrors renderNode() /
// blockFace() / renderMiningTrack() from the browser simulator.
//
// IMPORTANT: unlike the simulator (which faked the winner-selection locally
// with Math.random()), this screen only ever displays what the backend's
// mine-block Edge Function already decided -- see api_client.h's
// fetchMiningFeed() and backend/functions/mine-block/index.ts for where
// the actual winner selection happens. This file is purely a renderer.

#pragma once
#include <lvgl.h>
#include "api_client.h"
#include "ui/shared_components.h"
#include "ui/font_tenge.h"   // real ₸ glyph (Montserrat has no U+20B8)

#define NODE_MINED_BLOCKS_SHOWN 3
#define NODE_BLOCK_W      100   // mockup-sized cards (was 54 px — tiny)
#define NODE_BLOCK_H      116
#define NODE_BLOCK_SLOT_WIDTH (NODE_BLOCK_W + 8)

class NodeScreen {
public:
    lv_obj_t* build(lv_obj_t* parentScreen, lv_event_cb_t onLogoTapped, lv_event_cb_t onDateTapped,
                     lv_event_cb_t onQrTapped, lv_event_cb_t onVerifyBadgeTapped, void* userData) {
        header = buildSharedHeader(parentScreen, onLogoTapped, onDateTapped, userData);
        footer = buildSharedFooter(parentScreen, onQrTapped, userData);

        // Flex COLUMN body — the old build chained lv_obj_align_to() from
        // pre-layout coordinates, which piled everything up (the "totally
        // broken" screen). Structure mirrors the mockup: name+hint | UPTIME,
        // divider, TURBOUSD NETWORK | LIVE MINING, then the mining track.
        lv_obj_t* body = lv_obj_create(parentScreen);
        lv_obj_set_size(body, LV_PCT(100), 480 - 38 - 38);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_set_style_bg_color(body, lv_color_black(), 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 14, 0);
        lv_obj_set_style_pad_row(body, 8, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        // ── Countdown strip (mirrors the web header): NEXT BLOCK IN 48:12 → ₸100 ──
        lv_obj_t* cdRow = lv_obj_create(body);
        lv_obj_set_size(cdRow, LV_PCT(100), 24);
        lv_obj_set_style_bg_opa(cdRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(cdRow, 0, 0);
        lv_obj_set_style_pad_all(cdRow, 0, 0);
        lv_obj_set_style_pad_column(cdRow, 8, 0);
        lv_obj_set_flex_flow(cdRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cdRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(cdRow, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* cdCaption = lv_label_create(cdRow);
        lv_label_set_text(cdCaption, "NEXT BLOCK IN");
        lv_obj_set_style_text_color(cdCaption, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(cdCaption, &lv_font_montserrat_10, 0);

        countdownLabel = lv_label_create(cdRow);
        lv_label_set_text(countdownLabel, "--:--");
        lv_obj_set_style_text_color(countdownLabel, lv_color_hex(0xe8b339), 0);
        lv_obj_set_style_text_font(countdownLabel, &lv_font_montserrat_20, 0);

        countdownRewardLabel = lv_label_create(cdRow);
        lv_label_set_text(countdownRewardLabel, "");
        lv_obj_set_style_text_color(countdownRewardLabel, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_text_font(countdownRewardLabel, &lv_font_montserrat_12, 0);

        lv_obj_t* topRow = lv_obj_create(body);
        lv_obj_set_size(topRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(topRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(topRow, 0, 0);
        lv_obj_set_style_pad_all(topRow, 0, 0);
        lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

        // Left column: big yellow name + badge, verification hint below.
        lv_obj_t* leftCol = lv_obj_create(topRow);
        lv_obj_set_size(leftCol, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(leftCol, 1);
        lv_obj_set_style_bg_opa(leftCol, LV_OPA_0, 0);
        lv_obj_set_style_border_width(leftCol, 0, 0);
        lv_obj_set_style_pad_all(leftCol, 0, 0);
        lv_obj_set_style_pad_row(leftCol, 2, 0);
        lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* nameRow = lv_obj_create(leftCol);
        lv_obj_set_size(nameRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(nameRow, LV_OPA_0, 0);
        lv_obj_set_style_border_width(nameRow, 0, 0);
        lv_obj_set_style_pad_all(nameRow, 0, 0);
        lv_obj_set_style_pad_column(nameRow, 8, 0);
        lv_obj_set_flex_flow(nameRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(nameRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(nameRow, LV_OBJ_FLAG_SCROLLABLE);

        nodeNameLabel = lv_label_create(nameRow);
        lv_label_set_text(nodeNameLabel, "");
        lv_obj_set_style_text_color(nodeNameLabel, lv_color_hex(0xe8b339), 0);
        lv_obj_set_style_text_font(nodeNameLabel, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(nodeNameLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_style_max_width(nodeNameLabel, 250, 0);
        // Tap the big headline name → this node's info modal.
        lv_obj_add_flag(nodeNameLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(nodeNameLabel, 6);
        lv_obj_add_event_cb(nodeNameLabel, _onOwnNameTapped, LV_EVENT_CLICKED, this);

        verifyBadge = lv_label_create(nameRow);
        lv_label_set_text(verifyBadge, "");
        lv_obj_set_style_text_font(verifyBadge, &lv_font_montserrat_16, 0);
        // Diagonal strike drawn OVER the check for the "pending" state
        // (grey crossed-out check instead of the old hourglass).
        static lv_point_t strikePts[2] = { {0, 0}, {18, 16} };   // ↘ diagonal
        verifyStrike = lv_line_create(nameRow);
        lv_line_set_points(verifyStrike, strikePts, 2);
        lv_obj_set_style_line_width(verifyStrike, 3, 0);
        lv_obj_set_style_line_color(verifyStrike, lv_color_hex(0xe5484d), 0);   // red — unmistakably "not yet"
        lv_obj_set_style_line_rounded(verifyStrike, true, 0);
        lv_obj_add_flag(verifyStrike, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(verifyStrike, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(verifyBadge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(verifyBadge, 12);
        lv_obj_add_event_cb(verifyBadge, onVerifyBadgeTapped, LV_EVENT_CLICKED, userData);

        rewardsLabel = lv_label_create(leftCol);
        lv_label_set_text(rewardsLabel, "Get verified to start earning");
        lv_obj_set_style_text_color(rewardsLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(rewardsLabel, tengeFont12(), 0);   // "Rewards: ₸1.234"

        // Right column: UPTIME headline.
        lv_obj_t* uptimeCol = lv_obj_create(topRow);
        lv_obj_set_size(uptimeCol, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(uptimeCol, LV_OPA_0, 0);
        lv_obj_set_style_border_width(uptimeCol, 0, 0);
        lv_obj_set_style_pad_all(uptimeCol, 0, 0);
        lv_obj_set_style_pad_row(uptimeCol, 2, 0);
        lv_obj_set_flex_flow(uptimeCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(uptimeCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_clear_flag(uptimeCol, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* uptimeTitle = lv_label_create(uptimeCol);
        lv_label_set_text(uptimeTitle, "UPTIME");
        lv_obj_set_style_text_color(uptimeTitle, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(uptimeTitle, &lv_font_montserrat_10, 0);

        uptimeValueLabel = lv_label_create(uptimeCol);
        lv_label_set_text(uptimeValueLabel, "--");
        lv_obj_set_style_text_color(uptimeValueLabel, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_text_font(uptimeValueLabel, &lv_font_montserrat_20, 0);

        lv_obj_t* networkRow = lv_obj_create(body);
        lv_obj_set_size(networkRow, LV_PCT(100), 26);
        lv_obj_set_style_bg_opa(networkRow, LV_OPA_0, 0);
        lv_obj_set_style_border_side(networkRow, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(networkRow, lv_color_hex(0x2e2e34), 0);
        lv_obj_set_style_border_width(networkRow, 1, 0);
        lv_obj_set_style_pad_all(networkRow, 0, 0);
        lv_obj_set_style_pad_top(networkRow, 10, 0);
        lv_obj_clear_flag(networkRow, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* networkLabel = lv_label_create(networkRow);
        lv_label_set_text(networkLabel, "TURBOUSD NETWORK");
        lv_obj_set_style_text_color(networkLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(networkLabel, &lv_font_montserrat_10, 0);
        lv_obj_align(networkLabel, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* liveMiningLabel = lv_label_create(networkRow);
        lv_label_set_text(liveMiningLabel, "LIVE MINING");
        lv_obj_set_style_text_color(liveMiningLabel, lv_color_hex(0x3aff7a), 0);
        lv_obj_set_style_text_font(liveMiningLabel, &lv_font_montserrat_10, 0);
        lv_obj_align(liveMiningLabel, LV_ALIGN_RIGHT_MID, 0, 0);

        miningTrack = lv_obj_create(body);
        lv_obj_set_width(miningTrack, LV_PCT(100));
        lv_obj_set_height(miningTrack, NODE_BLOCK_H + 34);   // fixed: the leaderboard below takes the rest
        lv_obj_set_style_bg_opa(miningTrack, LV_OPA_0, 0);
        lv_obj_set_style_border_width(miningTrack, 0, 0);
        lv_obj_set_style_pad_all(miningTrack, 0, 0);
        lv_obj_clear_flag(miningTrack, LV_OBJ_FLAG_SCROLLABLE);

        dividerLine = lv_line_create(miningTrack);
        static lv_point_t dividerPoints[2];
        int dividerX = NODE_MINED_BLOCKS_SHOWN * NODE_BLOCK_SLOT_WIDTH + 6;
        dividerPoints[0] = { (lv_coord_t)dividerX, 8 };
        dividerPoints[1] = { (lv_coord_t)dividerX, (lv_coord_t)(NODE_BLOCK_H + 24) };
        lv_line_set_points(dividerLine, dividerPoints, 2);
        static lv_style_t dividerStyle;
        lv_style_init(&dividerStyle);
        lv_style_set_line_color(&dividerStyle, lv_color_hex(0xc4c4cc));
        lv_style_set_line_width(&dividerStyle, 3);
        lv_style_set_line_dash_width(&dividerStyle, 6);
        lv_style_set_line_dash_gap(&dividerStyle, 6);
        lv_obj_add_style(dividerLine, &dividerStyle, 0);

        for (int i = 0; i < NODE_MINED_BLOCKS_SHOWN; i++) {
            minedBlocks[i] = buildBlockWidget(miningTrack, true);
            styleBlock(minedBlocks[i], true);
            // Empty slots stay hidden until a real block fills them — three
            // blank green boxes read as "broken", not as "no data yet".
            lv_obj_add_flag(minedBlocks[i].container, LV_OBJ_FLAG_HIDDEN);
            // Newest mined block (i=0) sits rightmost, just left of the divider;
            // oldest (i = NODE_MINED_BLOCKS_SHOWN-1) sits leftmost.
            lv_coord_t slotX = (lv_coord_t)((NODE_MINED_BLOCKS_SHOWN - 1 - i) * NODE_BLOCK_SLOT_WIDTH);
            minedBlocks[i].restingX = slotX;   // remember home X for the slide anim
            lv_obj_set_pos(minedBlocks[i].container, slotX, 16);
            // Tap the winner name → node info modal (user_data = this block's index).
            if (minedBlocks[i].minerNameLabel) {
                lv_obj_add_flag(minedBlocks[i].minerNameLabel, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_ext_click_area(minedBlocks[i].minerNameLabel, 6);
                lv_obj_set_user_data(minedBlocks[i].minerNameLabel, (void*)(intptr_t)i);
                lv_obj_add_event_cb(minedBlocks[i].minerNameLabel, _onBlockNameTapped, LV_EVENT_CLICKED, this);
            }
        }
        pendingBlock = buildBlockWidget(miningTrack, false);
        // Pending block sits just right of the divider line. Offset +20 (not +18)
        // so its gap to the dashed line (14 px) matches the newest mined block's
        // gap on the other side — they were 12 vs 14 before (asymmetric).
        lv_obj_set_pos(pendingBlock.container,
                       (lv_coord_t)(NODE_MINED_BLOCKS_SHOWN * NODE_BLOCK_SLOT_WIDTH + 20), 16);
        lv_label_set_text(pendingBlock.numberLabel, "NEXT");

        // Local "always mining" animation: continuously fill the pending block's
        // ring over one block interval and loop. Previously the ring only moved
        // when the backend mining feed pushed progress — with the backend not yet
        // deployed the screen looked frozen. This on-device loop makes it read as
        // actively mining regardless of network state; if real feed data arrives,
        // updatePendingProgress() simply takes over the same ring.
        lv_anim_t ringAnim;
        lv_anim_init(&ringAnim);
        lv_anim_set_var(&ringAnim, pendingBlock.ring);
        lv_anim_set_values(&ringAnim, 0, 100);
        lv_anim_set_time(&ringAnim, 60000);   // ~one block per minute, visual only
        lv_anim_set_repeat_count(&ringAnim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&ringAnim, [](void* obj, int32_t v) {
            lv_arc_set_value((lv_obj_t*)obj, v);
        });
        lv_anim_start(&ringAnim);

        // ── Leaderboard: two columns (₸ rewards | uptime), like the web ──────
        lv_obj_t* lbRow = lv_obj_create(body);
        lv_obj_set_width(lbRow, LV_PCT(100));
        lv_obj_set_flex_grow(lbRow, 1);
        lv_obj_set_style_bg_opa(lbRow, LV_OPA_0, 0);
        lv_obj_set_style_border_side(lbRow, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(lbRow, lv_color_hex(0x2e2e34), 0);
        lv_obj_set_style_border_width(lbRow, 1, 0);
        lv_obj_set_style_pad_all(lbRow, 0, 0);
        lv_obj_set_style_pad_top(lbRow, 8, 0);
        lv_obj_set_style_pad_column(lbRow, 14, 0);
        lv_obj_set_flex_flow(lbRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(lbRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(lbRow, LV_OBJ_FLAG_SCROLLABLE);

        _buildLeaderColumn(lbRow, "\xE2\x82\xB8 REWARDS", 0,       lbRewardNames, lbRewardValues);
        _buildLeaderColumn(lbRow, "UPTIME",              LB_ROWS, lbUptimeNames, lbUptimeValues);

        // Soft pulse on the "LIVE MINING" label so there's a heartbeat even before
        // any blocks have been mined into the track.
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, liveMiningLabel);
        lv_anim_set_values(&pulse, LV_OPA_40, LV_OPA_COVER);
        lv_anim_set_time(&pulse, 1100);
        lv_anim_set_playback_time(&pulse, 1100);
        lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&pulse, [](void* obj, int32_t v) {
            lv_obj_set_style_text_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
        });
        lv_anim_start(&pulse);

        return body;
    }

    void setNodeName(const String& name) {
        lv_label_set_text(nodeNameLabel, name.c_str());
        strncpy(_ownName, name.c_str(), sizeof(_ownName) - 1);   // for the info modal
    }

    void setVerified(bool verified) {
        // Same CHECK glyph both ways: blue when verified, grey + diagonal
        // strike while verification is pending.
        lv_label_set_text(verifyBadge, "\xEF\x80\x8C");
        lv_obj_set_style_text_color(verifyBadge,
            verified ? lv_color_hex(0x1d9bf0) : lv_color_hex(0x6e7280), 0);
        if (verifyStrike) {
            if (verified) {
                lv_obj_add_flag(verifyStrike, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(verifyStrike, LV_OBJ_FLAG_HIDDEN);
                lv_obj_align_to(verifyStrike, verifyBadge, LV_ALIGN_CENTER, 0, 0);
            }
        }
    }

    void setUptime(const String& text) {
        lv_label_set_text(uptimeValueLabel, text.c_str());
        strncpy(_ownUptime, text.c_str(), sizeof(_ownUptime) - 1);   // for the info modal
    }

    void setRewards(bool verified, double tusdEarned) {
        _ownVerified = verified;   // for the info modal
        _ownEarned   = tusdEarned;
        if (verified) {
            char buf[40];
            snprintf(buf, sizeof(buf), "Rewards: \xE2\x82\xB8%.3f", tusdEarned);
            lv_label_set_text(rewardsLabel, buf);
            lv_obj_set_style_text_color(rewardsLabel, lv_color_hex(0x3aff7a), 0);
        } else {
            lv_label_set_text(rewardsLabel, "Get verified to start earning");
            lv_obj_set_style_text_color(rewardsLabel, lv_color_hex(0x6a6a6e), 0);
        }
    }

    // ── Node info modal ─────────────────────────────────────────────────────────
    // Tap a node name (the big headline name, a leaderboard row, or a mined-block
    // winner) → a green-bordered popup with that node's basic info, same modal
    // style as the verification tooltip. Genesis ⚡ and bio are omitted on device
    // (no lightning glyph in the built-in fonts; bio wouldn't fit).
    // Find a leaderboard row by display name (the own node + block winners are in
    // the directory) → lets any popup show country + twitter + real stats.
    LeaderboardEntry* _findSlotByName(const char* name) {
        if (!name || !name[0]) return nullptr;
        for (int i = 0; i < 2 * LB_ROWS; i++)
            if (_slotEntry[i].name[0] && strcmp(_slotEntry[i].name, name) == 0) return &_slotEntry[i];
        return nullptr;
    }

    void _showNodeInfo(const char* name, const char* country, const char* twitter,
                       bool hasStats, double earned, const char* uptimeStr) {
        if (g_touchWasSwipe()) return;   // a swipe that merely started on the name — not a tap
        lv_obj_t* card = openModal(lv_scr_act());

        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, (name && name[0]) ? name : "Node");
        lv_obj_set_style_text_color(title, lv_color_hex(0xe8b339), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(title, 250);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);   // else short names
                                                                      // sat left of the box
        // Twitter handle just below the name (blue), if the node set one.
        if (twitter && twitter[0]) {
            char tw[28]; snprintf(tw, sizeof(tw), "@%s", twitter);
            lv_obj_t* h = lv_label_create(card);
            lv_label_set_text(h, tw);
            lv_obj_set_style_text_color(h, lv_color_hex(0x5b8dee), 0);
            lv_obj_set_style_text_font(h, &lv_font_montserrat_12, 0);
        }

        if (country && country[0]) {
            lv_obj_t* loc = lv_label_create(card);
            lv_label_set_text(loc, country);
            lv_obj_set_style_text_color(loc, lv_color_hex(0x9a9a9e), 0);
            lv_obj_set_style_text_font(loc, &lv_font_montserrat_12, 0);
        }

        if (hasStats) {
            char buf[48];
            lv_obj_t* r = lv_label_create(card);
            snprintf(buf, sizeof(buf), "Rewards   \xE2\x82\xB8%.3f", earned);
            lv_label_set_text(r, buf);
            lv_obj_set_style_text_color(r, lv_color_hex(0x3aff7a), 0);
            lv_obj_set_style_text_font(r, tengeFont12(), 0);

            if (uptimeStr && uptimeStr[0]) {
                lv_obj_t* u = lv_label_create(card);
                snprintf(buf, sizeof(buf), "Uptime   %s", uptimeStr);
                lv_label_set_text(u, buf);
                lv_obj_set_style_text_color(u, lv_color_hex(0xe8e8e8), 0);
                lv_obj_set_style_text_font(u, &lv_font_montserrat_12, 0);
            }
        }
    }

    // Tap handlers — `this` comes via the event user_data; per-object user_data
    // identifies WHICH node (leaderboard slot index / mined-block index).
    static void _onOwnNameTapped(lv_event_t* e) {
        NodeScreen* s = (NodeScreen*)lv_event_get_user_data(e);
        // Enrich with country + twitter from the directory (the own node is in it);
        // always show stats (an unverified node still has ₸0 rewards + real uptime).
        LeaderboardEntry* d = s->_findSlotByName(s->_ownName);
        s->_showNodeInfo(s->_ownName, d ? d->country : "", d ? d->twitter : "",
                         true, s->_ownEarned, s->_ownUptime);
    }
    static void _onLbNameTapped(lv_event_t* e) {
        NodeScreen* s = (NodeScreen*)lv_event_get_user_data(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
        if (idx < 0 || idx >= 2 * LB_ROWS || !s->_slotEntry[idx].name[0]) return;
        LeaderboardEntry& en = s->_slotEntry[idx];
        char up[24]; _fmtUptimeShort(up, sizeof(up), en.totalUptimeSecs);
        s->_showNodeInfo(en.name, en.country, en.twitter, true, en.earned, up);
    }
    static void _onBlockNameTapped(lv_event_t* e) {
        NodeScreen* s = (NodeScreen*)lv_event_get_user_data(e);
        int bi = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
        if (bi < 0 || bi >= NODE_MINED_BLOCKS_SHOWN || !s->minedBlocks[bi].winnerName[0]) return;
        const char* wn = s->minedBlocks[bi].winnerName;
        const char* wc = s->minedBlocks[bi].winnerCountry;
        // Match the winner to a leaderboard row for rewards/uptime/twitter.
        LeaderboardEntry* d = s->_findSlotByName(wn);
        if (d) {
            char up[24]; _fmtUptimeShort(up, sizeof(up), d->totalUptimeSecs);
            s->_showNodeInfo(wn, wc, d->twitter, true, d->earned, up);
        } else {
            s->_showNodeInfo(wn, wc, "", false, 0, "");
        }
    }

    void updateMiningFeed(MiningFeedEntry* entries, int count) {
        if (!minedBlocks[0].container) return;   // feed arrived before build()
        int minedIdx = 0;
        for (int i = 0; i < count && minedIdx < NODE_MINED_BLOCKS_SHOWN; i++) {
            if (!entries[i].mined) continue;
            setBlockContent(minedBlocks[minedIdx], entries[i].blockNumber, entries[i].rewardTusd, entries[i].winnerDisplayName, entries[i].winnerCountry);
            minedIdx++;
        }
        // Hide any slots that no longer have a block (a shorter feed than before
        // used to leave stale blocks on screen). Reset lastBlockNumber so the
        // slot re-slides if it fills again later.
        for (int s = minedIdx; s < NODE_MINED_BLOCKS_SHOWN; s++) {
            if (!minedBlocks[s].container) continue;
            lv_anim_del(minedBlocks[s].container, nullptr);
            lv_obj_add_flag(minedBlocks[s].container, LV_OBJ_FLAG_HIDDEN);
            minedBlocks[s].lastBlockNumber = -1;
        }
        bool foundPending = false;
        for (int i = 0; i < count; i++) {
            if (entries[i].mined) continue;
            setPendingBlockNumber(entries[i].blockNumber);
            foundPending = true;
            break;
        }
        if (!foundPending && pendingBlock.numberLabel)
            lv_label_set_text(pendingBlock.numberLabel, "NEXT");   // no pending block yet
    }

    // Fill both leaderboard columns from the public node directory.
    void updateLeaderboard(LeaderboardEntry* entries, int count) {
        if (!lbRewardNames[0]) return;   // leaderboard not built yet
        // Top 3 by earnings. Cap raised so the real top-3 isn't computed from a
        // truncated first-N slice once the directory grows past a couple dozen.
        static LeaderboardEntry tmp[64];
        int n = count > 64 ? 64 : count;
        memcpy(tmp, entries, n * sizeof(LeaderboardEntry));
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (tmp[j].earned > tmp[i].earned) { LeaderboardEntry t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
        char buf[20];
        for (int i = 0; i < LB_ROWS; i++) {
            _slotEntry[i] = (i < n) ? tmp[i] : LeaderboardEntry{};   // for the info modal
            if (i < n) {
                lv_label_set_text(lbRewardNames[i], tmp[i].name);
                snprintf(buf, sizeof(buf), "\xE2\x82\xB8%.2f", tmp[i].earned);
                lv_label_set_text(lbRewardValues[i], buf);
            } else {
                lv_label_set_text(lbRewardNames[i], "");
                lv_label_set_text(lbRewardValues[i], "");
            }
        }
        // Top 3 by cumulative uptime (real time, not the %), matching the web card.
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (tmp[j].totalUptimeSecs > tmp[i].totalUptimeSecs) { LeaderboardEntry t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
        for (int i = 0; i < LB_ROWS; i++) {
            _slotEntry[LB_ROWS + i] = (i < n) ? tmp[i] : LeaderboardEntry{};   // for the info modal
            if (i < n) {
                lv_label_set_text(lbUptimeNames[i], tmp[i].name);
                _fmtUptimeShort(buf, sizeof(buf), tmp[i].totalUptimeSecs);
                lv_label_set_text(lbUptimeValues[i], buf);
            } else {
                lv_label_set_text(lbUptimeNames[i], "");
                lv_label_set_text(lbUptimeValues[i], "");
            }
        }
    }

    // "45s" / "12m" / "4h 58m" / "3d 4h" — same format as the web leaderboard.
    static void _fmtUptimeShort(char* buf, size_t sz, uint32_t s) {
        if      (s < 60)     snprintf(buf, sz, "%us", (unsigned)s);
        else if (s < 3600)   snprintf(buf, sz, "%um", (unsigned)(s / 60));
        else if (s < 86400)  snprintf(buf, sz, "%uh %um", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
        else                 snprintf(buf, sz, "%ud %uh", (unsigned)(s / 86400), (unsigned)((s % 86400) / 3600));
    }

    // Real countdown from the backend feed (pending block's created_at + 1 h).
    // Takes over from the fake looping animation the first time it's called.
    void updateCountdown(long secondsLeft, double rewardTusd) {
        if (!pendingBlock.ring || !pendingBlock.centerLabel) return;   // not built yet
        if (!_realCountdownActive) {
            _realCountdownActive = true;
            lv_anim_del(pendingBlock.ring, nullptr);   // stop the visual-only loop
        }
        if (secondsLeft < 0) secondsLeft = 0;
        // Ring drains as the hour elapses (full → empty), minutes in the middle.
        lv_arc_set_value(pendingBlock.ring, (int16_t)((secondsLeft * 100) / 3600));
        char mbuf[8];
        snprintf(mbuf, sizeof(mbuf), "%ld", (secondsLeft + 59) / 60);
        lv_label_set_text(pendingBlock.centerLabel, mbuf);

        char tbuf[12];
        snprintf(tbuf, sizeof(tbuf), "%02ld:%02ld", secondsLeft / 60, secondsLeft % 60);
        lv_label_set_text(countdownLabel, tbuf);
        (void)rewardTusd;
        if (countdownRewardLabel) lv_label_set_text(countdownRewardLabel, "");   // reward tag removed by request
    }

public:
    SharedHeaderRefs header;   // accessed by UIManager::refreshSharedAlarmIcon
    SharedFooterRefs footer;

private:
    lv_obj_t* nodeNameLabel = nullptr;
    lv_obj_t* verifyStrike = nullptr;
    lv_obj_t* verifyBadge = nullptr;
    lv_obj_t* uptimeValueLabel = nullptr;
    lv_obj_t* rewardsLabel = nullptr;
    lv_obj_t* miningTrack = nullptr;
    lv_obj_t* dividerLine = nullptr;
    lv_obj_t* countdownLabel = nullptr;
    lv_obj_t* countdownRewardLabel = nullptr;
    bool _realCountdownActive = false;

    static const int LB_ROWS = 3;
    lv_obj_t* lbRewardNames [LB_ROWS] = { nullptr };
    lv_obj_t* lbRewardValues[LB_ROWS] = { nullptr };
    lv_obj_t* lbUptimeNames [LB_ROWS] = { nullptr };
    lv_obj_t* lbUptimeValues[LB_ROWS] = { nullptr };

    // Stored node data for the tap-to-open info modal (see _showNodeInfo).
    char   _ownName[24]   = {};
    double _ownEarned     = 0;
    bool   _ownVerified   = false;
    char   _ownUptime[24] = {};
    LeaderboardEntry _slotEntry[2 * LB_ROWS] = {};   // reward rows 0..2, uptime rows 3..5

    // One leaderboard column: muted title + LB_ROWS rows of "name .... value".
    void _buildLeaderColumn(lv_obj_t* parent, const char* title, int colBase,
                            lv_obj_t** nameSlots, lv_obj_t** valueSlots) {
        lv_obj_t* col = lv_obj_create(parent);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_style_bg_opa(col, LV_OPA_0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_row(col, 4, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* titleLbl = lv_label_create(col);
        lv_label_set_text(titleLbl, title);
        lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(titleLbl, tengeFont10(), 0);   // "₸ REWARDS"

        for (int i = 0; i < LB_ROWS; i++) {
            lv_obj_t* row = lv_obj_create(col);
            lv_obj_set_size(row, LV_PCT(100), 18);
            lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

            nameSlots[i] = lv_label_create(row);
            lv_label_set_text(nameSlots[i], "");
            lv_obj_set_style_text_font(nameSlots[i], &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(nameSlots[i], lv_color_hex(0xe8e8e8), 0);
            lv_label_set_long_mode(nameSlots[i], LV_LABEL_LONG_DOT);
            lv_obj_set_style_max_width(nameSlots[i], 130, 0);
            // Tap a leaderboard name → node info modal (user_data = slot index).
            lv_obj_add_flag(nameSlots[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(nameSlots[i], 6);
            lv_obj_set_user_data(nameSlots[i], (void*)(intptr_t)(colBase + i));
            lv_obj_add_event_cb(nameSlots[i], _onLbNameTapped, LV_EVENT_CLICKED, this);

            valueSlots[i] = lv_label_create(row);
            lv_label_set_text(valueSlots[i], "");
            lv_obj_set_style_text_font(valueSlots[i], tengeFont12(), 0);   // "₸12.34"
            lv_obj_set_style_text_color(valueSlots[i], lv_color_hex(0x3aff7a), 0);
        }
    }

    struct BlockWidget {
        lv_obj_t* container = nullptr;
        lv_obj_t* numberLabel = nullptr;
        lv_obj_t* rewardLabel = nullptr;
        lv_obj_t* minerNameLabel = nullptr;
        lv_obj_t* minerCountryLabel = nullptr;   // country under the winner name
        lv_obj_t* ring = nullptr;
        lv_obj_t* centerLabel = nullptr;
        bool isMinedSlot = false;
        lv_coord_t restingX = 0;   // this slot's true home X (set once in build).
                                   // The slide anim must return here — reading
                                   // lv_obj_get_x() mid-slide captured a transient
                                   // position and the block drifted permanently.
        long lastBlockNumber = -1; // tracks what this slot last showed, so
                                    // setBlockContent() only animates a
                                    // slide when the slot's content actually
                                    // changes (a new block shifted in),
                                    // not on every periodic data refresh.
        char winnerName[24]    = {};   // stored for the tap-to-info modal
        char winnerCountry[40] = {};
    };

    BlockWidget minedBlocks[NODE_MINED_BLOCKS_SHOWN];
    BlockWidget pendingBlock;

    BlockWidget buildBlockWidget(lv_obj_t* parent, bool isMinedSlot) {
        BlockWidget w;
        w.isMinedSlot = isMinedSlot;

        w.container = lv_obj_create(parent);
        lv_obj_set_size(w.container, NODE_BLOCK_W, NODE_BLOCK_H);
        lv_obj_set_style_radius(w.container, 10, 0);
        lv_obj_set_style_border_width(w.container, 2, 0);
        lv_obj_set_style_pad_all(w.container, 6, 0);
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        styleBlock(w, false);

        if (isMinedSlot) {
            // Every label gets an explicit initial text — LVGL labels default
            // to the literal string "Text", which leaked onto the screen.
            w.numberLabel = lv_label_create(w.container);
            lv_label_set_text(w.numberLabel, "");
            lv_obj_set_style_text_font(w.numberLabel, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(w.numberLabel, lv_color_hex(0xd8ffe6), 0);
            lv_obj_align(w.numberLabel, LV_ALIGN_TOP_MID, 0, 6);

            w.rewardLabel = lv_label_create(w.container);
            lv_label_set_text(w.rewardLabel, "");
            lv_obj_set_style_text_font(w.rewardLabel, tengeFont20(), 0);   // "₸100"
            lv_obj_set_style_text_color(w.rewardLabel, lv_color_white(), 0);
            // Nudged up so the number/reward/name/country stack is evenly
            // spaced now that a country line sits at the bottom.
            lv_obj_align(w.rewardLabel, LV_ALIGN_CENTER, 0, -8);

            w.minerNameLabel = lv_label_create(w.container);
            lv_label_set_text(w.minerNameLabel, "");
            lv_obj_set_width(w.minerNameLabel, NODE_BLOCK_W - 14);
            lv_label_set_long_mode(w.minerNameLabel, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(w.minerNameLabel, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(w.minerNameLabel, lv_color_hex(0xd8ffe6), 0);
            lv_obj_set_style_text_align(w.minerNameLabel, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(w.minerNameLabel, LV_ALIGN_BOTTOM_MID, 0, -20);   // nudged up to
                                                                          // make room for the
                                                                          // bigger country line
            // Country under the name — dim green, one line, ellipsised. Bumped
            // 8→10 px (montserrat_9 isn't compiled in) so it's actually readable.
            w.minerCountryLabel = lv_label_create(w.container);
            lv_label_set_text(w.minerCountryLabel, "");
            lv_obj_set_width(w.minerCountryLabel, NODE_BLOCK_W - 14);
            lv_label_set_long_mode(w.minerCountryLabel, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(w.minerCountryLabel, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(w.minerCountryLabel, lv_color_hex(0x89b39a), 0);
            lv_obj_set_style_text_align(w.minerCountryLabel, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(w.minerCountryLabel, LV_ALIGN_BOTTOM_MID, 0, -4);
        } else {
            w.numberLabel = lv_label_create(w.container);
            lv_label_set_text(w.numberLabel, "");
            lv_obj_set_style_text_font(w.numberLabel, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(w.numberLabel, lv_color_hex(0xffe9b8), 0);
            lv_obj_align(w.numberLabel, LV_ALIGN_TOP_MID, 0, 6);

            w.ring = lv_arc_create(w.container);
            lv_obj_set_size(w.ring, 56, 56);
            lv_arc_set_rotation(w.ring, 270);
            lv_arc_set_bg_angles(w.ring, 0, 360);
            lv_arc_set_range(w.ring, 0, 100);
            lv_arc_set_value(w.ring, 0);
            lv_obj_remove_style(w.ring, NULL, LV_PART_KNOB);
            lv_obj_clear_flag(w.ring, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_color(w.ring, lv_color_hex(0xe8b339), LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(w.ring, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_arc_opa(w.ring, LV_OPA_40, LV_PART_MAIN);
            lv_obj_set_style_arc_width(w.ring, 5, LV_PART_MAIN);
            lv_obj_set_style_arc_width(w.ring, 5, LV_PART_INDICATOR);
            lv_obj_align(w.ring, LV_ALIGN_CENTER, 0, 8);

            w.centerLabel = lv_label_create(w.ring);
            lv_label_set_text(w.centerLabel, "--");   // no default "Text"
            lv_obj_set_style_text_font(w.centerLabel, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(w.centerLabel, lv_color_white(), 0);
            lv_obj_center(w.centerLabel);
        }

        return w;
    }

    void styleBlock(BlockWidget& w, bool mined) {
        lv_obj_set_style_bg_color(w.container, mined ? lv_color_hex(0x1c5c30) : lv_color_hex(0x5a3d0c), 0);
        lv_obj_set_style_border_color(w.container, mined ? lv_color_hex(0x3aff7a) : lv_color_hex(0xe8b339), 0);
        // Slight 3D volume: a soft coloured drop shadow to the lower-LEFT (like
        // the web tiles), so the flat cards read as chunky blocks.
        lv_obj_set_style_shadow_width(w.container, 12, 0);
        lv_obj_set_style_shadow_spread(w.container, 0, 0);
        lv_obj_set_style_shadow_ofs_x(w.container, -4, 0);
        lv_obj_set_style_shadow_ofs_y(w.container, 7, 0);
        lv_obj_set_style_shadow_opa(w.container, LV_OPA_60, 0);
        lv_obj_set_style_shadow_color(w.container, mined ? lv_color_hex(0x0c3a20) : lv_color_hex(0x3a2c08), 0);
    }

    void setBlockContent(BlockWidget& w, long blockNumber, double reward, const String& minerName,
                         const String& country = String("")) {
        bool isNewBlock = (w.lastBlockNumber != blockNumber);
        w.lastBlockNumber = blockNumber;
        // Remember the winner for the tap-to-open info modal.
        strncpy(w.winnerName,    minerName.c_str(), sizeof(w.winnerName) - 1);
        strncpy(w.winnerCountry, country.c_str(),   sizeof(w.winnerCountry) - 1);

        char numBuf[12]; snprintf(numBuf, sizeof(numBuf), "#%ld", blockNumber);
        lv_label_set_text(w.numberLabel, numBuf);
        char rewardBuf[16]; snprintf(rewardBuf, sizeof(rewardBuf), "\xE2\x82\xB8%d", (int)reward);
        lv_label_set_text(w.rewardLabel, rewardBuf);
        lv_label_set_text(w.minerNameLabel, minerName.length() ? minerName.c_str() : "--");
        if (w.minerCountryLabel) {
            // Location is always the anonymized (~300 km) IP-derived country; no
            // marker needed here (the "i" info note lives on the web cards).
            lv_label_set_text(w.minerCountryLabel, country.c_str());
        }
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_HIDDEN);

        if (!isNewBlock) return; // periodic refresh of the same block's data, no slide needed

        // Slide in from one slot-width to the right of this widget's HOME
        // position, mirroring the simulator's `transition: left 0.9s ease-in-out`
        // -- the block visually arrives from where the next-older slot used to
        // be, reading as "everything shifted left". Use the stored resting X (not
        // the live x, which may be mid-slide) and cancel any in-flight slide so
        // overlapping animations can't leave the block parked off-position.
        lv_anim_del(w.container, nullptr);
        lv_coord_t restingX = w.restingX;
        lv_obj_set_x(w.container, restingX + NODE_BLOCK_SLOT_WIDTH);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, w.container);
        lv_anim_set_values(&anim, restingX + NODE_BLOCK_SLOT_WIDTH, restingX);
        lv_anim_set_time(&anim, 700);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, [](void* obj, int32_t x) { lv_obj_set_x((lv_obj_t*)obj, x); });
        lv_anim_start(&anim);
    }

    void setPendingBlockNumber(long blockNumber) {
        char buf[8]; snprintf(buf, sizeof(buf), "#%ld", blockNumber);
        lv_label_set_text(pendingBlock.numberLabel, buf);
    }
};
