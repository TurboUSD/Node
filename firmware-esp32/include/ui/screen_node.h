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

        verifyBadge = lv_label_create(nameRow);
        lv_label_set_text(verifyBadge, "");
        lv_obj_set_style_text_font(verifyBadge, &lv_font_montserrat_16, 0);
        lv_obj_add_flag(verifyBadge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(verifyBadge, 12);
        lv_obj_add_event_cb(verifyBadge, onVerifyBadgeTapped, LV_EVENT_CLICKED, userData);

        rewardsLabel = lv_label_create(leftCol);
        lv_label_set_text(rewardsLabel, "Get verified to start earning");
        lv_obj_set_style_text_color(rewardsLabel, lv_color_hex(0x9a9a9e), 0);
        lv_obj_set_style_text_font(rewardsLabel, &lv_font_montserrat_12, 0);

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
        lv_obj_set_flex_grow(miningTrack, 1);
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
            lv_obj_set_pos(minedBlocks[i].container, slotX, 16);
        }
        pendingBlock = buildBlockWidget(miningTrack, false);
        // Pending block sits just right of the divider line
        lv_obj_set_pos(pendingBlock.container,
                       (lv_coord_t)(NODE_MINED_BLOCKS_SHOWN * NODE_BLOCK_SLOT_WIDTH + 18), 16);
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

    void setNodeName(const String& name) { lv_label_set_text(nodeNameLabel, name.c_str()); }

    void setVerified(bool verified) {
        lv_label_set_text(verifyBadge, verified ? "\xEF\x80\x8C" : "\xEF\x80\xA1");
        lv_obj_set_style_text_color(verifyBadge, verified ? lv_color_hex(0x1d9bf0) : lv_color_hex(0xe8b339), 0);
    }

    void setUptime(const String& text) { lv_label_set_text(uptimeValueLabel, text.c_str()); }

    void setRewards(bool verified, double tusdEarned) {
        if (verified) {
            char buf[40];
            snprintf(buf, sizeof(buf), "Rewards: %.3f TUSD", tusdEarned);
            lv_label_set_text(rewardsLabel, buf);
            lv_obj_set_style_text_color(rewardsLabel, lv_color_hex(0x3aff7a), 0);
        } else {
            lv_label_set_text(rewardsLabel, "Get verified to start earning");
            lv_obj_set_style_text_color(rewardsLabel, lv_color_hex(0x6a6a6e), 0);
        }
    }

    void updateMiningFeed(MiningFeedEntry* entries, int count) {
        int minedIdx = 0;
        for (int i = 0; i < count && minedIdx < NODE_MINED_BLOCKS_SHOWN; i++) {
            if (!entries[i].mined) continue;
            setBlockContent(minedBlocks[minedIdx], entries[i].blockNumber, entries[i].rewardTusd, entries[i].winnerDisplayName);
            minedIdx++;
        }
        for (int i = 0; i < count; i++) {
            if (entries[i].mined) continue;
            setPendingBlockNumber(entries[i].blockNumber);
            break;
        }
    }

    void updatePendingProgress(float progress01, int simulatedMinutesLeft) {
        lv_arc_set_value(pendingBlock.ring, (int16_t)(progress01 * 100));
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", simulatedMinutesLeft);
        lv_label_set_text(pendingBlock.centerLabel, buf);
    }

public:
    SharedHeaderRefs header;   // accessed by UIManager::refreshSharedAlarmIcon
    SharedFooterRefs footer;

private:
    lv_obj_t* nodeNameLabel = nullptr;
    lv_obj_t* verifyBadge = nullptr;
    lv_obj_t* uptimeValueLabel = nullptr;
    lv_obj_t* rewardsLabel = nullptr;
    lv_obj_t* miningTrack = nullptr;
    lv_obj_t* dividerLine = nullptr;

    struct BlockWidget {
        lv_obj_t* container = nullptr;
        lv_obj_t* numberLabel = nullptr;
        lv_obj_t* rewardLabel = nullptr;
        lv_obj_t* minerNameLabel = nullptr;
        lv_obj_t* ring = nullptr;
        lv_obj_t* centerLabel = nullptr;
        bool isMinedSlot = false;
        long lastBlockNumber = -1; // tracks what this slot last showed, so
                                    // setBlockContent() only animates a
                                    // slide when the slot's content actually
                                    // changes (a new block shifted in),
                                    // not on every periodic data refresh.
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
            w.numberLabel = lv_label_create(w.container);
            lv_obj_set_style_text_font(w.numberLabel, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(w.numberLabel, lv_color_hex(0xd8ffe6), 0);
            lv_obj_align(w.numberLabel, LV_ALIGN_TOP_MID, 0, 6);

            w.rewardLabel = lv_label_create(w.container);
            lv_obj_set_style_text_font(w.rewardLabel, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(w.rewardLabel, lv_color_white(), 0);
            lv_obj_align(w.rewardLabel, LV_ALIGN_CENTER, 0, -2);

            w.minerNameLabel = lv_label_create(w.container);
            lv_obj_set_width(w.minerNameLabel, NODE_BLOCK_W - 14);
            lv_label_set_long_mode(w.minerNameLabel, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(w.minerNameLabel, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(w.minerNameLabel, lv_color_hex(0xd8ffe6), 0);
            lv_obj_set_style_text_align(w.minerNameLabel, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(w.minerNameLabel, LV_ALIGN_BOTTOM_MID, 0, -5);
        } else {
            w.numberLabel = lv_label_create(w.container);
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
            lv_obj_set_style_text_font(w.centerLabel, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(w.centerLabel, lv_color_white(), 0);
            lv_obj_center(w.centerLabel);
        }

        return w;
    }

    void styleBlock(BlockWidget& w, bool mined) {
        lv_obj_set_style_bg_color(w.container, mined ? lv_color_hex(0x1c5c30) : lv_color_hex(0x5a3d0c), 0);
        lv_obj_set_style_border_color(w.container, mined ? lv_color_hex(0x3aff7a) : lv_color_hex(0xe8b339), 0);
    }

    void setBlockContent(BlockWidget& w, long blockNumber, double reward, const String& minerName) {
        bool isNewBlock = (w.lastBlockNumber != blockNumber);
        w.lastBlockNumber = blockNumber;

        char numBuf[12]; snprintf(numBuf, sizeof(numBuf), "#%ld", blockNumber);
        lv_label_set_text(w.numberLabel, numBuf);
        char rewardBuf[16]; snprintf(rewardBuf, sizeof(rewardBuf), "T%d", (int)reward);
        lv_label_set_text(w.rewardLabel, rewardBuf);
        lv_label_set_text(w.minerNameLabel, minerName.length() ? minerName.c_str() : "--");
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_HIDDEN);

        if (!isNewBlock) return; // periodic refresh of the same block's data, no slide needed

        // Slide in from one slot-width to the right of this widget's
        // resting position, mirroring the simulator's `transition: left
        // 0.9s ease-in-out` -- the block visually arrives from where the
        // next-older slot used to be, reading as "everything shifted left".
        lv_coord_t restingX = lv_obj_get_x(w.container);
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
