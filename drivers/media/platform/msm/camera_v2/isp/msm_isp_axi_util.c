/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/io.h>
#include <media/v4l2-subdev.h>
#include "msm_isp_util.h"
#include "msm_isp_axi_util.h"

#define SRC_TO_INTF(src) \
	((src < RDI_INTF_0) ? VFE_PIX_0 : \
	(VFE_RAW_0 + src - RDI_INTF_0))

#define HANDLE_TO_IDX(handle) (handle & 0xFF)

int msm_isp_axi_create_stream(
	struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream_request_cmd *stream_cfg_cmd)
{
	int i, rc = -1;
	for (i = 0; i < MAX_NUM_STREAM; i++) {
		if (axi_data->stream_info[i].state == AVALIABLE)
			break;
	}

	if (i == MAX_NUM_STREAM) {
		pr_err("%s: No free stream\n", __func__);
		return rc;
	}

	axi_data->created_streams_num++;

	if ((axi_data->stream_handle_cnt << 8) == 0)
		axi_data->stream_handle_cnt++;

	stream_cfg_cmd->axi_stream_handle =
		(++axi_data->stream_handle_cnt) << 8 | i;

	memset(&axi_data->stream_info[i], 0,
		   sizeof(struct msm_vfe_axi_stream));
	axi_data->stream_info[i].session_id = stream_cfg_cmd->session_id;
	axi_data->stream_info[i].stream_id = stream_cfg_cmd->stream_id;
	axi_data->stream_info[i].buf_divert = stream_cfg_cmd->buf_divert;
	axi_data->stream_info[i].state = INACTIVE;
	axi_data->stream_info[i].stream_handle =
		stream_cfg_cmd->axi_stream_handle;
	return 0;
}

void msm_isp_axi_destroy_stream(
	struct msm_vfe_axi_shared_data *axi_data, int stream_idx)
{
	if (axi_data->stream_info[stream_idx].state != AVALIABLE) {
		axi_data->stream_info[stream_idx].state = AVALIABLE;
		axi_data->stream_info[stream_idx].stream_handle = 0;
		axi_data->created_streams_num--;
	} else {
		pr_err("%s: stream does not exist\n", __func__);
	}
}

int msm_isp_validate_axi_request(struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream_request_cmd *stream_cfg_cmd)
{
	int rc = -1, i;
	struct msm_vfe_axi_stream *stream_info =
		&axi_data->stream_info[
			HANDLE_TO_IDX(stream_cfg_cmd->axi_stream_handle)];

	switch (stream_cfg_cmd->output_format) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_QBGGR8:
	case V4L2_PIX_FMT_QGBRG8:
	case V4L2_PIX_FMT_QGRBG8:
	case V4L2_PIX_FMT_QRGGB8:
	case V4L2_PIX_FMT_QBGGR10:
	case V4L2_PIX_FMT_QGBRG10:
	case V4L2_PIX_FMT_QGRBG10:
	case V4L2_PIX_FMT_QRGGB10:
	case V4L2_PIX_FMT_QBGGR12:
	case V4L2_PIX_FMT_QGBRG12:
	case V4L2_PIX_FMT_QGRBG12:
	case V4L2_PIX_FMT_QRGGB12:
		stream_info->num_planes = 1;
		stream_info->format_factor = ISP_Q2;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		stream_info->num_planes = 2;
		stream_info->format_factor = 1.5 * ISP_Q2;
		break;
	/*TD: Add more image format*/
	default:
		pr_err("%s: Invalid output format\n", __func__);
		return rc;
	}

	if (axi_data->hw_info->num_wm - axi_data->num_used_wm <
		stream_info->num_planes) {
		pr_err("%s: No free write masters\n", __func__);
		return rc;
	}

	if ((stream_info->num_planes > 1) &&
			(axi_data->hw_info->num_comp_mask -
			axi_data->num_used_composite_mask < 1)) {
		pr_err("%s: No free composite mask\n", __func__);
		return rc;
	}

	if (stream_cfg_cmd->init_frame_drop >= MAX_INIT_FRAME_DROP) {
		pr_err("%s: Invalid skip pattern\n", __func__);
		return rc;
	}

	if (stream_cfg_cmd->frame_skip_pattern >= MAX_SKIP) {
		pr_err("%s: Invalid skip pattern\n", __func__);
		return rc;
	}

	for (i = 0; i < stream_info->num_planes; i++) {
		stream_info->plane_cfg[i] = stream_cfg_cmd->plane_cfg[i];
		stream_info->max_width = max(stream_info->max_width,
			stream_cfg_cmd->plane_cfg[i].output_width);
	}

	stream_info->output_format = stream_cfg_cmd->output_format;
	stream_info->stream_src = stream_cfg_cmd->stream_src;
	stream_info->frame_based = stream_cfg_cmd->frame_base;
	return 0;
}

static uint32_t msm_isp_axi_get_plane_size(
	struct msm_vfe_axi_stream *stream_info, int plane_idx)
{
	uint32_t size = 0;
	struct msm_vfe_axi_plane_cfg *plane_cfg = stream_info->plane_cfg;
	switch (stream_info->output_format) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_QBGGR8:
	case V4L2_PIX_FMT_QGBRG8:
	case V4L2_PIX_FMT_QGRBG8:
	case V4L2_PIX_FMT_QRGGB8:
		size = plane_cfg[plane_idx].output_height *
		plane_cfg[plane_idx].output_width;
		break;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_QBGGR10:
	case V4L2_PIX_FMT_QGBRG10:
	case V4L2_PIX_FMT_QGRBG10:
	case V4L2_PIX_FMT_QRGGB10:
		/* TODO: fix me */
		size = plane_cfg[plane_idx].output_height *
		plane_cfg[plane_idx].output_width;
		break;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_QBGGR12:
	case V4L2_PIX_FMT_QGBRG12:
	case V4L2_PIX_FMT_QGRBG12:
	case V4L2_PIX_FMT_QRGGB12:
		/* TODO: fix me */
		size = plane_cfg[plane_idx].output_height *
		plane_cfg[plane_idx].output_width;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		if (plane_cfg[plane_idx].output_plane_format == Y_PLANE)
			size = plane_cfg[plane_idx].output_height *
				plane_cfg[plane_idx].output_width;
		else
			size = plane_cfg[plane_idx].output_height *
				plane_cfg[plane_idx].output_width / 2;
		break;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		size = plane_cfg[plane_idx].output_height *
			plane_cfg[plane_idx].output_width;
		break;
	/*TD: Add more image format*/
	default:
		pr_err("%s: Invalid output format\n", __func__);
		break;
	}
	return size;
}

void msm_isp_axi_reserve_wm(struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream *stream_info)
{
	int i, j;
	for (i = 0; i < stream_info->num_planes; i++) {
		for (j = 0; j < axi_data->hw_info->num_wm; j++) {
			if (!axi_data->free_wm[j]) {
				axi_data->free_wm[j] =
					stream_info->stream_handle;
				axi_data->wm_image_size[j] =
					msm_isp_axi_get_plane_size(
						stream_info, i);
				axi_data->num_used_wm++;
				break;
			}
		}
		stream_info->wm[i] = j;
	}
}

void msm_isp_axi_free_wm(struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream *stream_info)
{
	int i;
	for (i = 0; i < stream_info->num_planes; i++) {
		axi_data->free_wm[stream_info->wm[i]] = 0;
		axi_data->num_used_wm--;
	}
}

void msm_isp_axi_reserve_comp_mask(
	struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream *stream_info)
{
	int i;
	uint8_t comp_mask = 0;
	for (i = 0; i < stream_info->num_planes; i++)
		comp_mask |= 1 << stream_info->wm[i];

	for (i = 0; i < axi_data->hw_info->num_comp_mask; i++) {
		if (!axi_data->composite_info[i].stream_handle) {
			axi_data->composite_info[i].stream_handle =
				stream_info->stream_handle;
			axi_data->composite_info[i].
				stream_composite_mask = comp_mask;
			axi_data->num_used_composite_mask++;
			break;
		}
	}
	stream_info->comp_mask_index = i;
	return;
}

void msm_isp_axi_free_comp_mask(struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream *stream_info)
{
	axi_data->composite_info[stream_info->comp_mask_index].
		stream_composite_mask = 0;
	axi_data->composite_info[stream_info->comp_mask_index].
		stream_handle = 0;
	axi_data->num_used_composite_mask--;
}

int msm_isp_axi_get_bufq_handles(
		struct vfe_device *vfe_dev,
		struct msm_vfe_axi_stream *stream_info)
{
	int rc = 0;

	if (stream_info->stream_id & ISP_SCRATCH_BUF_BIT) {
		stream_info->bufq_handle =
			vfe_dev->buf_mgr->ops->get_bufq_handle(
		vfe_dev->buf_mgr, stream_info->session_id,
		stream_info->stream_id & ~ISP_SCRATCH_BUF_BIT);
		if (stream_info->bufq_handle == 0) {
			pr_err("%s: Stream 0x%x has no valid buffer queue\n",
				__func__, (unsigned int)stream_info->stream_id);
			rc = -EINVAL;
			return rc;
		}

		stream_info->bufq_scratch_handle =
			vfe_dev->buf_mgr->ops->get_bufq_handle(
		vfe_dev->buf_mgr, stream_info->session_id,
		stream_info->stream_id);
		if (stream_info->bufq_scratch_handle == 0) {
			pr_err("%s: Stream 0x%x has no valid buffer queue\n",
				__func__, (unsigned int)stream_info->stream_id);
			rc = -EINVAL;
			return rc;
		}
	} else {
		stream_info->bufq_handle =
			vfe_dev->buf_mgr->ops->get_bufq_handle(
		vfe_dev->buf_mgr, stream_info->session_id,
		stream_info->stream_id);
		if (stream_info->bufq_handle == 0) {
			pr_err("%s: Stream 0x%x has no valid buffer queue\n",
				__func__, (unsigned int)stream_info->stream_id);
			rc = -EINVAL;
			return rc;
		}
	}
	return rc;
}

int msm_isp_axi_check_stream_state(
	struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream_cfg_cmd *stream_cfg_cmd)
{
	int rc = 0, i;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	struct msm_vfe_axi_stream *stream_info;
	enum msm_vfe_axi_state valid_state =
		(stream_cfg_cmd->cmd == START_STREAM) ? INACTIVE : ACTIVE;

	for (i = 0; i < stream_cfg_cmd->num_streams; i++) {
		if (HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i]) >=
			MAX_NUM_STREAM) {
 			return -EINVAL;
 		}
		stream_info = &axi_data->stream_info[
			HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i])];
		if (stream_info->state != valid_state) {
			pr_err("%s: Invalid stream state\n", __func__);
			rc = -EINVAL;
			break;
		}

		if (stream_cfg_cmd->cmd == START_STREAM) {
			rc = msm_isp_axi_get_bufq_handles(vfe_dev, stream_info);
			if (rc)
				break;
		}
	}
	return rc;
}

void msm_isp_update_framedrop_reg(struct vfe_device *vfe_dev)
{
	int i;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	struct msm_vfe_axi_stream *stream_info;
	for (i = 0; i < MAX_NUM_STREAM; i++) {
		stream_info = &axi_data->stream_info[i];
		if (stream_info->state != ACTIVE)
			continue;

		if (stream_info->runtime_framedrop_update) {
			stream_info->runtime_init_frame_drop--;
			if (stream_info->runtime_init_frame_drop == 0) {
				stream_info->runtime_framedrop_update = 0;
				vfe_dev->hw_info->vfe_ops.axi_ops.
				cfg_framedrop(vfe_dev, stream_info);
			}
		}
		if (stream_info->stream_type == BURST_STREAM) {
			stream_info->runtime_burst_frame_count--;
			if (stream_info->runtime_burst_frame_count == 0) {
				vfe_dev->hw_info->vfe_ops.axi_ops.
				cfg_framedrop(vfe_dev, stream_info);
				vfe_dev->hw_info->vfe_ops.core_ops.
				 reg_update(vfe_dev);
			}
		}
	}
}

static void msm_isp_reset_framedrop(struct vfe_device *vfe_dev,
			struct msm_vfe_axi_stream *stream_info)
{
	stream_info->runtime_init_frame_drop = stream_info->init_frame_drop;
	stream_info->runtime_burst_frame_count =
		stream_info->burst_frame_count;
	stream_info->runtime_num_burst_capture =
		stream_info->num_burst_capture;
	stream_info->runtime_framedrop_update = stream_info->framedrop_update;
	vfe_dev->hw_info->vfe_ops.axi_ops.cfg_framedrop(vfe_dev, stream_info);
}

void msm_isp_sof_notify(struct vfe_device *vfe_dev,
	enum msm_vfe_input_src frame_src, struct msm_isp_timestamp *ts) {
	struct msm_isp_event_data sof_event;

	vfe_dev->skip_ping_pong_cfg = 0;
	if (vfe_dev->skip_isp_send_event) {
		vfe_dev->skip_ping_pong_cfg = 1;
		return;
	}

	switch (frame_src) {
	case VFE_PIX_0:
		vfe_dev->axi_data.src_info[VFE_PIX_0].frame_id++;
		if (vfe_dev->axi_data.src_info[VFE_PIX_0].frame_id == 0)
			vfe_dev->axi_data.src_info[VFE_PIX_0].frame_id = 1;
		ISP_DBG("%s: PIX0 frame id: %lu\n", __func__,
			vfe_dev->axi_data.src_info[VFE_PIX_0].frame_id);
		break;
	case VFE_RAW_0:
	case VFE_RAW_1:
	case VFE_RAW_2:
		vfe_dev->axi_data.src_info[frame_src].frame_id++;
		if (vfe_dev->axi_data.src_info[frame_src].frame_id == 0)
			vfe_dev->axi_data.src_info[frame_src].frame_id = 1;
		ISP_DBG("%s: RDI%d frame id: %lu\n",
			__func__, frame_src - VFE_RAW_0,
			vfe_dev->axi_data.src_info[frame_src].frame_id);
		break;
	default:
		pr_err("%s: invalid frame src %d received\n",
			__func__, frame_src);
		break;
	}

	sof_event.frame_id = vfe_dev->axi_data.src_info[frame_src].frame_id;
	sof_event.timestamp = ts->event_time;
	sof_event.mono_timestamp = ts->buf_time;
	msm_isp_send_event(vfe_dev, ISP_EVENT_SOF, &sof_event);
}

void msm_isp_eof_notify(struct vfe_device *vfe_dev)
{
	unsigned long flags;

	vfe_dev->skip_isp_send_event = 0;

	spin_lock_irqsave(&vfe_dev->cfg_flag_lock, flags);
	if (vfe_dev->config_done_flag != 1 &&
			(vfe_dev->axi_data.num_active_stream ==
				vfe_dev->axi_data.created_streams_num)) {
		vfe_dev->skip_isp_send_event = 1;
		ISP_DBG("%s: Skip frame because ISP cfg is not done! \n",
				__func__);
	}
	vfe_dev->config_done_flag = 0;
	spin_unlock_irqrestore(&vfe_dev->cfg_flag_lock, flags);
}

void msm_isp_calculate_framedrop(
	struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream_request_cmd *stream_cfg_cmd)
{
	struct msm_vfe_axi_stream *stream_info =
		&axi_data->stream_info[
		HANDLE_TO_IDX(stream_cfg_cmd->axi_stream_handle)];
	uint32_t framedrop_period = msm_isp_get_framedrop_period(
	   stream_cfg_cmd->frame_skip_pattern);

	if (stream_cfg_cmd->frame_skip_pattern == SKIP_ALL)
		stream_info->framedrop_pattern = 0x0;
	else
		stream_info->framedrop_pattern = 0x1;
	stream_info->framedrop_period = framedrop_period - 1;

	if (stream_cfg_cmd->init_frame_drop < framedrop_period) {
		stream_info->framedrop_pattern <<=
			stream_cfg_cmd->init_frame_drop;
		stream_info->init_frame_drop = 0;
		stream_info->framedrop_update = 0;
	} else {
		stream_info->init_frame_drop = stream_cfg_cmd->init_frame_drop;
		stream_info->framedrop_update = 1;
	}

	if (stream_cfg_cmd->burst_count > 0) {
		stream_info->stream_type = BURST_STREAM;
		stream_info->num_burst_capture =
			stream_cfg_cmd->burst_count;
		stream_info->burst_frame_count =
		stream_cfg_cmd->init_frame_drop +
			(stream_cfg_cmd->burst_count - 1) *
			framedrop_period + 1;
	} else {
		stream_info->stream_type = CONTINUOUS_STREAM;
		stream_info->burst_frame_count = 0;
		stream_info->num_burst_capture = 0;
	}
}

void msm_isp_calculate_bandwidth(
	struct msm_vfe_axi_shared_data *axi_data,
	struct msm_vfe_axi_stream *stream_info)
{
	if (stream_info->stream_src < RDI_INTF_0) {
		stream_info->bandwidth =
			(axi_data->src_info[VFE_PIX_0].pixel_clock /
			axi_data->src_info[VFE_PIX_0].width) *
			stream_info->max_width;
		stream_info->bandwidth = stream_info->bandwidth *
			stream_info->format_factor / ISP_Q2;
	} else {
		int rdi = SRC_TO_INTF(stream_info->stream_src);
		stream_info->bandwidth = axi_data->src_info[rdi].pixel_clock;
	}
}

int msm_isp_request_axi_stream(struct vfe_device *vfe_dev, void *arg)
{
	int rc = 0, i;
	struct msm_vfe_axi_stream_request_cmd *stream_cfg_cmd = arg;
	struct msm_vfe_axi_stream *stream_info;

	rc = msm_isp_axi_create_stream(
		&vfe_dev->axi_data, stream_cfg_cmd);
	if (rc) {
		pr_err("%s: create stream failed\n", __func__);
		return rc;
	}

	if (vfe_dev->axi_data.created_streams_num == 1)
		vfe_dev->config_done_flag = 0;

	rc = msm_isp_validate_axi_request(
		&vfe_dev->axi_data, stream_cfg_cmd);
	if (rc) {
		pr_err("%s: Request validation failed\n", __func__);
		if (HANDLE_TO_IDX(stream_cfg_cmd->axi_stream_handle) <
			MAX_NUM_STREAM)
			msm_isp_axi_destroy_stream(&vfe_dev->axi_data,
			      HANDLE_TO_IDX(stream_cfg_cmd->axi_stream_handle));
		return rc;
	}

	stream_info = &vfe_dev->axi_data.
		stream_info[HANDLE_TO_IDX(stream_cfg_cmd->axi_stream_handle)];
	msm_isp_axi_reserve_wm(&vfe_dev->axi_data, stream_info);

	if (stream_cfg_cmd->stream_src == CAMIF_RAW ||
		stream_cfg_cmd->stream_src == IDEAL_RAW)
			vfe_dev->hw_info->vfe_ops.axi_ops.
				cfg_io_format(vfe_dev, stream_info);

	msm_isp_calculate_framedrop(&vfe_dev->axi_data, stream_cfg_cmd);

	if (stream_info->num_planes > 1) {
		msm_isp_axi_reserve_comp_mask(
			&vfe_dev->axi_data, stream_info);
		vfe_dev->hw_info->vfe_ops.axi_ops.
		cfg_comp_mask(vfe_dev, stream_info);
	} else {
		vfe_dev->hw_info->vfe_ops.axi_ops.
			cfg_wm_irq_mask(vfe_dev, stream_info);
	}

	for (i = 0; i < stream_info->num_planes; i++) {
		vfe_dev->hw_info->vfe_ops.axi_ops.
			cfg_wm_reg(vfe_dev, stream_info, i);

		vfe_dev->hw_info->vfe_ops.axi_ops.
			cfg_wm_xbar_reg(vfe_dev, stream_info, i);
	}
	return rc;
}

int msm_isp_release_axi_stream(struct vfe_device *vfe_dev, void *arg)
{
	int rc = 0, i;
	struct msm_vfe_axi_stream_release_cmd *stream_release_cmd = arg;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_stream_cfg_cmd stream_cfg;


	if (HANDLE_TO_IDX(stream_release_cmd->stream_handle) >=
		MAX_NUM_STREAM) {
		pr_err("%s: Invalid stream handle\n", __func__);
		return -EINVAL;
	}
	stream_info = &axi_data->stream_info[
		HANDLE_TO_IDX(stream_release_cmd->stream_handle)];
	if (stream_info->state == AVALIABLE) {
		pr_err("%s: Stream already released\n", __func__);
		return -EINVAL;
	} else if (stream_info->state != INACTIVE) {
		stream_cfg.cmd = STOP_STREAM;
		stream_cfg.num_streams = 1;
		stream_cfg.stream_handle[0] = stream_release_cmd->stream_handle;
		msm_isp_cfg_axi_stream(vfe_dev, (void *) &stream_cfg);
	}

	for (i = 0; i < stream_info->num_planes; i++) {
		vfe_dev->hw_info->vfe_ops.axi_ops.
			clear_wm_reg(vfe_dev, stream_info, i);

		vfe_dev->hw_info->vfe_ops.axi_ops.
		clear_wm_xbar_reg(vfe_dev, stream_info, i);
	}

	if (stream_info->num_planes > 1) {
		vfe_dev->hw_info->vfe_ops.axi_ops.
			clear_comp_mask(vfe_dev, stream_info);
		msm_isp_axi_free_comp_mask(&vfe_dev->axi_data, stream_info);
	} else {
		vfe_dev->hw_info->vfe_ops.axi_ops.
		clear_wm_irq_mask(vfe_dev, stream_info);
	}

	vfe_dev->hw_info->vfe_ops.axi_ops.clear_framedrop(vfe_dev, stream_info);
	msm_isp_axi_free_wm(axi_data, stream_info);

	msm_isp_axi_destroy_stream(&vfe_dev->axi_data,
		HANDLE_TO_IDX(stream_release_cmd->stream_handle));

	return rc;
}

static void msm_isp_axi_stream_enable_cfg(
	struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream *stream_info)
{
	int i;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	uint32_t stream_idx;
	if (stream_info->state == INACTIVE)
		return;
	for (i = 0; i < stream_info->num_planes; i++) {
		if (stream_info->state == START_PENDING)
			vfe_dev->hw_info->vfe_ops.axi_ops.
				enable_wm(vfe_dev, stream_info->wm[i], 1);
		else
			vfe_dev->hw_info->vfe_ops.axi_ops.
				enable_wm(vfe_dev, stream_info->wm[i], 0);
	}
	stream_idx = HANDLE_TO_IDX(stream_info->stream_handle);
	if (stream_info->state == START_PENDING) {
		axi_data->num_active_stream++;
	} else {
		axi_data->num_active_stream--;
		axi_data->proc_done_data[stream_idx].data_for_send = 0;
	}
}

void msm_isp_axi_stream_update(struct vfe_device *vfe_dev)
{
	int i;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	for (i = 0; i < MAX_NUM_STREAM; i++) {
		if (axi_data->stream_info[i].state == START_PENDING ||
				axi_data->stream_info[i].state ==
					STOP_PENDING) {
			msm_isp_axi_stream_enable_cfg(
				vfe_dev, &axi_data->stream_info[i]);
			axi_data->stream_info[i].state =
				axi_data->stream_info[i].state ==
				START_PENDING ? STARTING : STOPPING;
		} else if (axi_data->stream_info[i].state == STARTING ||
			axi_data->stream_info[i].state == STOPPING) {
			axi_data->stream_info[i].state =
				axi_data->stream_info[i].state == STARTING ?
				ACTIVE : INACTIVE;
		}
	}

	if (vfe_dev->axi_data.pipeline_update == DISABLE_CAMIF ||
		(vfe_dev->axi_data.pipeline_update ==
		DISABLE_CAMIF_IMMEDIATELY)) {
		vfe_dev->hw_info->vfe_ops.stats_ops.
			enable_module(vfe_dev, 0xFF, 0);
		vfe_dev->axi_data.pipeline_update = NO_UPDATE;
	}

	vfe_dev->axi_data.stream_update--;
	if (vfe_dev->axi_data.stream_update == 0)
		complete(&vfe_dev->stream_config_complete);
}

static void msm_isp_cfg_pong_address(struct vfe_device *vfe_dev,
		struct msm_vfe_axi_stream *stream_info)
{
	int i;
	struct msm_isp_buffer *buf = stream_info->buf[0];
	for (i = 0; i < stream_info->num_planes; i++)
		vfe_dev->hw_info->vfe_ops.axi_ops.update_ping_pong_addr(
			vfe_dev, stream_info->wm[i],
			VFE_PONG_FLAG, buf->mapped_info[i].paddr +
			stream_info->plane_cfg[i].plane_addr_offset);
	stream_info->buf[1] = buf;
}

static void msm_isp_get_done_buf(struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream *stream_info, uint32_t pingpong_status,
	struct msm_isp_buffer **done_buf)
{
	uint32_t pingpong_bit = 0, i;
	pingpong_bit = (~(pingpong_status >> stream_info->wm[0]) & 0x1);
	for (i = 0; i < stream_info->num_planes; i++) {
		if (pingpong_bit !=
			(~(pingpong_status >> stream_info->wm[i]) & 0x1)) {
			pr_warn("%s: Write master ping pong mismatch. Status: 0x%x\n",
				__func__, pingpong_status);
		}
	}
	*done_buf = stream_info->buf[pingpong_bit];
}

static int msm_isp_cfg_ping_pong_address(struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream *stream_info, uint32_t pingpong_status)
{
	int i, rc = -1;
	struct msm_isp_buffer *buf = NULL;
	uint32_t pingpong_bit = 0;
	uint32_t bufq_handle = 0;
	uint32_t stream_idx = HANDLE_TO_IDX(stream_info->stream_handle);

	if (stream_info->bufq_scratch_handle && !stream_info->request_frame)
		bufq_handle = stream_info->bufq_scratch_handle;
	else
		bufq_handle = stream_info->bufq_handle;

	rc = vfe_dev->buf_mgr->ops->get_buf(vfe_dev->buf_mgr,
			vfe_dev->pdev->id, bufq_handle, &buf);

	if (rc < 0) {
		vfe_dev->error_info.stream_framedrop_count[stream_idx]++;
		if (stream_info->bufq_scratch_handle &&
				stream_info->request_frame)
			pr_err("%s: Error: v4l2 buff is missing!\n", __func__);
		return rc;
	}

	if (stream_info->bufq_scratch_handle &&
			bufq_handle == stream_info->bufq_handle)
		stream_info->request_frame = 0;

	if (buf->num_planes != stream_info->num_planes) {
		pr_err("%s: Invalid buffer\n", __func__);
		rc = -EINVAL;
		goto buf_error;
	}

	for (i = 0; i < stream_info->num_planes; i++)
		vfe_dev->hw_info->vfe_ops.axi_ops.update_ping_pong_addr(
			vfe_dev, stream_info->wm[i],
			pingpong_status, buf->mapped_info[i].paddr +
			stream_info->plane_cfg[i].plane_addr_offset);

	pingpong_bit = (~(pingpong_status >> stream_info->wm[0]) & 0x1);
	stream_info->buf[pingpong_bit] = buf;
	return 0;
buf_error:
	vfe_dev->buf_mgr->ops->put_buf(vfe_dev->buf_mgr,
		buf->bufq_handle, buf->buf_idx);
	return rc;
}

static void msm_isp_process_frame_drop(struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream *stream_info,
	struct msm_isp_timestamp *ts)
{
	struct msm_isp_event_data buf_event;
	uint32_t frame_id = vfe_dev->axi_data.
		src_info[SRC_TO_INTF(stream_info->stream_src)].frame_id;

	buf_event.frame_id = frame_id;
	buf_event.timestamp = ts->buf_time;
	buf_event.u.buf_done.session_id =
		stream_info->session_id;
	buf_event.u.buf_done.stream_id =
		stream_info->stream_id;
	buf_event.u.buf_done.handle = 0;
	buf_event.u.buf_done.buf_idx = 0;
	msm_isp_send_event(vfe_dev, ISP_EVENT_FRAME_DROP, &buf_event);
}

static void msm_isp_process_done_buf(struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream *stream_info, struct msm_isp_buffer *buf,
	struct msm_isp_timestamp *ts)
{
	int rc;
	struct msm_isp_event_data buf_event;
	uint32_t stream_idx = HANDLE_TO_IDX(stream_info->stream_handle);
	uint32_t buf_src;
	uint32_t frame_id;

	if (stream_idx >= MAX_NUM_STREAM) {
		pr_err("%s: Invalid stream_idx", __func__);
		return;
	}

	frame_id = vfe_dev->axi_data.
		src_info[SRC_TO_INTF(stream_info->stream_src)].frame_id;

	if (buf && ts) {
		rc = vfe_dev->buf_mgr->ops->get_buf_src(vfe_dev->buf_mgr,
						buf->bufq_handle, &buf_src);
		if (stream_info->buf_divert && rc == 0 &&
				buf_src != MSM_ISP_BUFFER_SRC_SCRATCH) {
			rc = vfe_dev->buf_mgr->ops->buf_divert(vfe_dev->buf_mgr,
				buf->bufq_handle, buf->buf_idx,
				&ts->buf_time, frame_id);
			/* Buf divert return value represent whether the buf
			 * can be diverted. A positive return value means
			 * other ISP hardware is still processing the frame.
			 */
			if (rc == 0) {
				buf_event.frame_id = frame_id;
				buf_event.timestamp = ts->buf_time;
				buf_event.u.buf_done.session_id =
					stream_info->session_id;
				buf_event.u.buf_done.stream_id =
					stream_info->stream_id;
				buf_event.u.buf_done.handle =
					stream_info->bufq_handle;
				buf_event.u.buf_done.buf_idx = buf->buf_idx;
				msm_isp_send_event(vfe_dev,
					ISP_EVENT_BUF_DIVERT + stream_idx,
					&buf_event);
			}
		} else {
			vfe_dev->buf_mgr->ops->buf_done(vfe_dev->buf_mgr,
				buf->bufq_handle, buf->buf_idx,
				&ts->buf_time, frame_id);
		}
	}
}

static enum msm_isp_camif_update_state
	msm_isp_get_camif_update_state(struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream_cfg_cmd *stream_cfg_cmd)
{
	int i;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	uint8_t pix_stream_cnt = 0, raw_stream_cnt = 0;
	uint8_t cur_pix_stream_cnt, cur_raw_stream_cnt;
	uint8_t stream_cnt, cur_stream_cnt;

	cur_pix_stream_cnt = axi_data->src_info[VFE_PIX_0].pix_stream_count;
	cur_raw_stream_cnt = axi_data->src_info[VFE_PIX_0].raw_stream_count;
	cur_stream_cnt = cur_pix_stream_cnt + cur_raw_stream_cnt;

	for (i = 0; i < stream_cfg_cmd->num_streams; i++) {
		stream_info =
			&axi_data->stream_info[
			HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i])];
		if (stream_info->stream_src < CAMIF_RAW)
			pix_stream_cnt++;
		else if (stream_info->stream_src < RDI_INTF_0)
			raw_stream_cnt++;
	}
	stream_cnt = pix_stream_cnt + raw_stream_cnt;

	if (stream_cnt) {
		if (((cur_stream_cnt == 0) ||
			(raw_stream_cnt && cur_raw_stream_cnt == 0)) &&
			stream_cfg_cmd->cmd == START_STREAM)
			return ENABLE_CAMIF;
		else if (cur_stream_cnt &&
			(cur_stream_cnt - stream_cnt) == 0 &&
			stream_cfg_cmd->cmd == STOP_STREAM)
			return DISABLE_CAMIF;
		else if ((cur_pix_stream_cnt || cur_raw_stream_cnt) &&
			(cur_pix_stream_cnt - pix_stream_cnt) == 0 &&
                        (cur_raw_stream_cnt - raw_stream_cnt) == 0 &&
			stream_cfg_cmd->cmd == STOP_IMMEDIATELY)
			return DISABLE_CAMIF_IMMEDIATELY;
	}
	return NO_UPDATE;
}

static void msm_isp_update_camif_output_count(
	struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream_cfg_cmd *stream_cfg_cmd)
{
	int i;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	for (i = 0; i < stream_cfg_cmd->num_streams; i++) {
		if (HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i]) >=
			MAX_NUM_STREAM) {
 			return;
 		}
		stream_info =
			&axi_data->stream_info[
			HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i])];
		if (stream_info->stream_src >= RDI_INTF_0)
			return;
		if (stream_info->stream_src == PIX_ENCODER ||
			stream_info->stream_src == PIX_VIEWFINDER ||
			stream_info->stream_src == IDEAL_RAW) {
			if (stream_cfg_cmd->cmd == START_STREAM)
				vfe_dev->axi_data.src_info[VFE_PIX_0].
					pix_stream_count++;
			else
				vfe_dev->axi_data.src_info[VFE_PIX_0].
					pix_stream_count--;
		} else if (stream_info->stream_src == CAMIF_RAW) {
			if (stream_cfg_cmd->cmd == START_STREAM)
				vfe_dev->axi_data.src_info[VFE_PIX_0].
					raw_stream_count++;
			else
				vfe_dev->axi_data.src_info[VFE_PIX_0].
					raw_stream_count--;
		}
	}
}

void msm_camera_io_dump_2(void __iomem *addr, int size)
{
	char line_str[128], *p_str;
	int i;
	u32 *p = (u32 *) addr;
	u32 data;
	ISP_DBG("%s: %p %d\n", __func__, addr, size);
	line_str[0] = '\0';
	p_str = line_str;
	for (i = 0; i < size/4; i++) {
		if (i % 4 == 0) {
			snprintf(p_str, 12, "%08x: ", (u32) p);
			p_str += 10;
		}
		data = readl_relaxed(p++);
		snprintf(p_str, 12, "%08x ", data);
		p_str += 9;
		if ((i + 1) % 4 == 0) {
			ISP_DBG("%s\n", line_str);
			line_str[0] = '\0';
			p_str = line_str;
		}
	}
	if (line_str[0] != '\0')
		ISP_DBG("%s\n", line_str);
}

/*Factor in Q2 format*/
#define ISP_DEFAULT_FORMAT_FACTOR 6
#define ISP_BUS_UTILIZATION_FACTOR 6
static int msm_isp_update_stream_bandwidth(struct vfe_device *vfe_dev)
{
	int i, rc = 0;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	uint32_t total_pix_bandwidth = 0, total_rdi_bandwidth = 0;
	uint32_t num_pix_streams = 0;
	uint64_t total_bandwidth = 0;

	for (i = 0; i < MAX_NUM_STREAM; i++) {
		stream_info = &axi_data->stream_info[i];
		if (stream_info->state == ACTIVE ||
			stream_info->state == START_PENDING) {
			if (stream_info->stream_src < RDI_INTF_0) {
				total_pix_bandwidth += stream_info->bandwidth;
				num_pix_streams++;
			} else {
				total_rdi_bandwidth += stream_info->bandwidth;
			}
		}
	}
	if (num_pix_streams > 0)
		total_pix_bandwidth = total_pix_bandwidth /
			num_pix_streams * (num_pix_streams - 1) +
			axi_data->src_info[VFE_PIX_0].pixel_clock *
			ISP_DEFAULT_FORMAT_FACTOR / ISP_Q2;
	total_bandwidth = total_pix_bandwidth + total_rdi_bandwidth;

	rc = msm_isp_update_bandwidth(ISP_VFE0 + vfe_dev->pdev->id,
		total_bandwidth, total_bandwidth *
		ISP_BUS_UTILIZATION_FACTOR / ISP_Q2);
	if (rc < 0)
		pr_err("%s: update failed\n", __func__);

	return rc;
}

static int msm_isp_axi_wait_for_cfg_done(struct vfe_device *vfe_dev,
	enum msm_isp_camif_update_state camif_update)
{
	int rc;
	unsigned long flags;
	spin_lock_irqsave(&vfe_dev->shared_data_lock, flags);
	init_completion(&vfe_dev->stream_config_complete);
	vfe_dev->axi_data.pipeline_update = camif_update;
	vfe_dev->axi_data.stream_update = 2;
	spin_unlock_irqrestore(&vfe_dev->shared_data_lock, flags);
	rc = wait_for_completion_interruptible_timeout(
		&vfe_dev->stream_config_complete,
		msecs_to_jiffies(VFE_MAX_CFG_TIMEOUT));
	if (rc == 0) {
		pr_err("%s: wait timeout\n", __func__);
		rc = -1;
	} else {
		rc = 0;
	}
	return rc;
}

static int msm_isp_init_stream_ping_pong_reg(
	struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream *stream_info)
{
	int rc = 0;
	/*Set address for both PING & PONG register */
	rc = msm_isp_cfg_ping_pong_address(vfe_dev,
		stream_info, VFE_PING_FLAG);
	if (rc < 0) {
		pr_err("%s: No free buffer for ping\n",
			   __func__);
		return rc;
	}

	/* For burst stream of one capture, only one buffer
	 * is allocated. Duplicate ping buffer address to pong
	 * buffer to ensure hardware write to a valid address
	 */
	if (stream_info->stream_type == BURST_STREAM &&
		stream_info->runtime_num_burst_capture <= 1) {
		msm_isp_cfg_pong_address(vfe_dev, stream_info);
	} else {
		rc = msm_isp_cfg_ping_pong_address(vfe_dev,
			stream_info, VFE_PONG_FLAG);
		if (rc < 0) {
			pr_err("%s: No free buffer for pong\n",
				   __func__);
			return rc;
		}
	}
	return rc;
}

static void msm_isp_deinit_stream_ping_pong_reg(
	struct vfe_device *vfe_dev,
	struct msm_vfe_axi_stream *stream_info)
{
	int i;
	for (i = 0; i < 2; i++) {
		struct msm_isp_buffer *buf;
		buf = stream_info->buf[i];
		if (buf)
			vfe_dev->buf_mgr->ops->put_buf(vfe_dev->buf_mgr,
				buf->bufq_handle, buf->buf_idx);
	}
}

static void msm_isp_get_stream_wm_mask(
	struct msm_vfe_axi_stream *stream_info,
	uint32_t *wm_reload_mask)
{
	int i;
	for (i = 0; i < stream_info->num_planes; i++)
		*wm_reload_mask |= (1 << stream_info->wm[i]);
}

static int msm_isp_start_axi_stream(struct vfe_device *vfe_dev,
			struct msm_vfe_axi_stream_cfg_cmd *stream_cfg_cmd,
			enum msm_isp_camif_update_state camif_update)
{
	int i, rc = 0;
	uint8_t src_state, wait_for_complete = 0;
	uint32_t wm_reload_mask = 0x0;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	for (i = 0; i < stream_cfg_cmd->num_streams; i++) {
		if (HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i]) >=
			MAX_NUM_STREAM) {
 			return -EINVAL;
 		}
		stream_info = &axi_data->stream_info[
			HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i])];
		src_state = axi_data->src_info[
			SRC_TO_INTF(stream_info->stream_src)].active;

		msm_isp_calculate_bandwidth(axi_data, stream_info);
		msm_isp_reset_framedrop(vfe_dev, stream_info);
		msm_isp_get_stream_wm_mask(stream_info, &wm_reload_mask);
		rc = msm_isp_init_stream_ping_pong_reg(vfe_dev, stream_info);
		if (rc < 0) {
			pr_err("%s: No buffer for stream%d\n", __func__,
				HANDLE_TO_IDX(
				stream_cfg_cmd->stream_handle[i]));
			return rc;
		}

		stream_info->state = START_PENDING;
		if (src_state) {
			wait_for_complete = 1;
		} else {
			if (vfe_dev->dump_reg)
				msm_camera_io_dump_2(vfe_dev->vfe_base, 0x900);

			/*Configure AXI start bits to start immediately*/
			msm_isp_axi_stream_enable_cfg(vfe_dev, stream_info);
			stream_info->state = ACTIVE;
		}
	}
	msm_isp_update_stream_bandwidth(vfe_dev);
	vfe_dev->hw_info->vfe_ops.axi_ops.reload_wm(vfe_dev, wm_reload_mask);
	vfe_dev->hw_info->vfe_ops.core_ops.reg_update(vfe_dev);

	msm_isp_update_camif_output_count(vfe_dev, stream_cfg_cmd);
	if (camif_update == ENABLE_CAMIF)
		vfe_dev->hw_info->vfe_ops.core_ops.
			update_camif_state(vfe_dev, camif_update);

	if (wait_for_complete)
		rc = msm_isp_axi_wait_for_cfg_done(vfe_dev, camif_update);

	return rc;
}

static int msm_isp_stop_axi_stream(struct vfe_device *vfe_dev,
			struct msm_vfe_axi_stream_cfg_cmd *stream_cfg_cmd,
			enum msm_isp_camif_update_state camif_update)
{
	int i, rc = 0;
	uint8_t wait_for_complete = 0;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	for (i = 0; i < stream_cfg_cmd->num_streams; i++) {
		if (HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i]) >=
			MAX_NUM_STREAM) {
 			return -EINVAL;
 		}
		stream_info = &axi_data->stream_info[
		     HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i])];


		stream_info->state = STOP_PENDING;
		if (stream_info->stream_type == BURST_STREAM &&
			stream_info->runtime_num_burst_capture == 0) {
			/*Configure AXI writemasters to stop immediately
			 *since for burst case, write masters already skip
			 *all frames.
			 */
			msm_isp_axi_stream_enable_cfg(vfe_dev, stream_info);
			stream_info->state = INACTIVE;
		} else {
			wait_for_complete = 1;
		}
	}

	if (wait_for_complete) {
		rc = msm_isp_axi_wait_for_cfg_done(vfe_dev, camif_update);
		if (rc < 0) {
			pr_err("%s: wait for config done failed\n", __func__);
			return rc;
		}
	}
	msm_isp_update_stream_bandwidth(vfe_dev);
	if (camif_update == DISABLE_CAMIF)
		vfe_dev->hw_info->vfe_ops.core_ops.
			update_camif_state(vfe_dev, DISABLE_CAMIF);
	else if (camif_update == DISABLE_CAMIF_IMMEDIATELY)
		vfe_dev->hw_info->vfe_ops.core_ops.
			update_camif_state(vfe_dev, DISABLE_CAMIF_IMMEDIATELY);
	msm_isp_update_camif_output_count(vfe_dev, stream_cfg_cmd);

	for (i = 0; i < stream_cfg_cmd->num_streams; i++) {
		stream_info = &axi_data->stream_info[
			HANDLE_TO_IDX(stream_cfg_cmd->stream_handle[i])];
		msm_isp_deinit_stream_ping_pong_reg(vfe_dev, stream_info);
	}
	return rc;
}


int msm_isp_cfg_axi_stream(struct vfe_device *vfe_dev, void *arg)
{
	int rc = 0;
	struct msm_vfe_axi_stream_cfg_cmd *stream_cfg_cmd = arg;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	enum msm_isp_camif_update_state camif_update;

	rc = msm_isp_axi_check_stream_state(vfe_dev, stream_cfg_cmd);
	if (rc < 0) {
		pr_err("%s: Invalid stream state\n", __func__);
		return rc;
	}

	if (axi_data->num_active_stream == 0) {
		/*Configure UB*/
		vfe_dev->hw_info->vfe_ops.axi_ops.cfg_ub(vfe_dev);
	}
	camif_update = msm_isp_get_camif_update_state(vfe_dev, stream_cfg_cmd);

	if (stream_cfg_cmd->cmd == START_STREAM)
		rc = msm_isp_start_axi_stream(
		   vfe_dev, stream_cfg_cmd, camif_update);
	else
		rc = msm_isp_stop_axi_stream(
		   vfe_dev, stream_cfg_cmd, camif_update);

	if (rc < 0)
		pr_err("%s: start/stop stream failed\n", __func__);
	return rc;
}

int msm_isp_update_axi_stream(struct vfe_device *vfe_dev, void *arg)
{
	int rc = 0;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	struct msm_vfe_axi_stream_update_cmd *update_cmd = arg;
	stream_info = &axi_data->stream_info[
			HANDLE_TO_IDX(update_cmd->stream_handle)];
	if (stream_info->state != ACTIVE && stream_info->state != INACTIVE) {
		pr_err("%s: Invalid stream state\n", __func__);
		return -EINVAL;
	}

	switch (update_cmd->update_type) {
	case ENABLE_STREAM_BUF_DIVERT:
		stream_info->buf_divert = 1;
		break;
	case DISABLE_STREAM_BUF_DIVERT:
		stream_info->buf_divert = 0;
		vfe_dev->buf_mgr->ops->flush_buf(vfe_dev->buf_mgr,
				stream_info->bufq_handle,
				MSM_ISP_BUFFER_FLUSH_DIVERTED);
		break;
	case UPDATE_STREAM_FRAMEDROP_PATTERN: {
		uint32_t framedrop_period =
			msm_isp_get_framedrop_period(update_cmd->skip_pattern);
		stream_info->runtime_init_frame_drop = 0;
		stream_info->framedrop_pattern = 0x1;
		stream_info->framedrop_period = framedrop_period - 1;
		vfe_dev->hw_info->vfe_ops.axi_ops.
			cfg_framedrop(vfe_dev, stream_info);
		break;
	}
	case UPDATE_STREAM_REQUEST_FRAMES:
		stream_info->request_frame = 1;
		break;
	default:
		pr_err("%s: Invalid update type\n", __func__);
		return -EINVAL;
	}
	return rc;
}

void msm_isp_process_axi_irq(struct vfe_device *vfe_dev,
	uint32_t irq_status0, uint32_t irq_status1,
	struct msm_isp_timestamp *ts)
{
	int i, rc = 0;
	struct msm_isp_buffer *done_buf = NULL;
	uint32_t comp_mask = 0, wm_mask = 0;
	uint32_t pingpong_status, stream_idx;
	struct msm_vfe_axi_stream *stream_info;
	struct msm_vfe_axi_composite_info *comp_info;
	struct msm_vfe_axi_shared_data *axi_data = &vfe_dev->axi_data;
	struct msm_vfe_axi_process_done_data *proc_done_data =
			vfe_dev->axi_data.proc_done_data;

	comp_mask = vfe_dev->hw_info->vfe_ops.axi_ops.
		get_comp_mask(irq_status0, irq_status1);
	wm_mask = vfe_dev->hw_info->vfe_ops.axi_ops.
		get_wm_mask(irq_status0, irq_status1);
	if (!(comp_mask || wm_mask))
		return;

	ISP_DBG("%s: status: 0x%x\n", __func__, irq_status0);
	pingpong_status =
		vfe_dev->hw_info->vfe_ops.axi_ops.get_pingpong_status(vfe_dev);

	/* Process buffer done for previous frame in case of frame drop. */
	if (vfe_dev->skip_ping_pong_cfg) {
		for (i = 0; i < MAX_NUM_STREAM; i++) {
			if (!proc_done_data[i].data_for_send)
				continue;

			done_buf = proc_done_data[i].done_buf_arr;
			proc_done_data[i].data_for_send = 0;
			stream_info = &axi_data->stream_info[i];
			stream_info->frame_id++;

			msm_isp_process_done_buf(vfe_dev, stream_info,
					done_buf, ts);
		}
		comp_mask = 0;
		wm_mask = 0;
		return;
	}

	for (i = 0; i < axi_data->hw_info->num_comp_mask; i++) {
		comp_info = &axi_data->composite_info[i];
		if (comp_mask & (1 << i)) {
			stream_idx = HANDLE_TO_IDX(comp_info->stream_handle);
			if ((!comp_info->stream_handle) ||
				(stream_idx >= MAX_NUM_STREAM)) {
				pr_err("%s: Invalid handle for composite irq\n",
					__func__);
				continue;
			}
			stream_idx = HANDLE_TO_IDX(comp_info->stream_handle);
			stream_info = &axi_data->stream_info[stream_idx];

			if (vfe_dev->skip_isp_send_event) {
				msm_isp_get_done_buf(vfe_dev, stream_info,
					pingpong_status, &done_buf);

				if (stream_info->stream_type ==
					CONTINUOUS_STREAM ||
					stream_info->
					runtime_num_burst_capture > 1) {
					rc = msm_isp_cfg_ping_pong_address(
							vfe_dev, stream_info,
							pingpong_status);
				}
				proc_done_data[stream_idx].done_buf_arr = done_buf;
				proc_done_data[stream_idx].data_for_send = 1;
			} else {
				ISP_DBG("%s: stream%d frame id: 0x%x\n",
					__func__,
					stream_idx, stream_info->frame_id);
				stream_info->frame_id++;

				if (stream_info->stream_type == BURST_STREAM)
					stream_info->
						runtime_num_burst_capture--;

				msm_isp_get_done_buf(vfe_dev, stream_info,
					pingpong_status, &done_buf);
				if (stream_info->stream_type ==
					CONTINUOUS_STREAM ||
					stream_info->
					runtime_num_burst_capture > 1) {
					rc = msm_isp_cfg_ping_pong_address(
							vfe_dev, stream_info,
							pingpong_status);
				}
				if (done_buf && !rc)
					msm_isp_process_done_buf(vfe_dev,
					stream_info, done_buf, ts);
				if (done_buf && rc)
					/* Propagate frame drop */
					msm_isp_process_frame_drop(vfe_dev,
					stream_info, ts);
			}
		}
		wm_mask &= ~(comp_info->stream_composite_mask);
	}

	for (i = 0; i < axi_data->hw_info->num_wm; i++) {
		if (wm_mask & (1 << i)) {
			stream_idx = HANDLE_TO_IDX(axi_data->free_wm[i]);
			if ((!axi_data->free_wm[i]) ||
				(stream_idx >= MAX_NUM_STREAM)) {
				pr_err("%s: Invalid handle for wm irq\n",
					__func__);
				continue;
			}
			stream_idx = HANDLE_TO_IDX(axi_data->free_wm[i]);
			stream_info = &axi_data->stream_info[stream_idx];

			if (vfe_dev->skip_isp_send_event) {
				msm_isp_get_done_buf(vfe_dev, stream_info,
							pingpong_status, &done_buf);
				if (stream_info->stream_type == CONTINUOUS_STREAM ||
					stream_info->runtime_num_burst_capture > 1) {
					rc = msm_isp_cfg_ping_pong_address(vfe_dev,
						stream_info, pingpong_status);
				}

				proc_done_data[stream_idx].done_buf_arr = done_buf;
				proc_done_data[stream_idx].data_for_send = 1;
			} else {
				ISP_DBG("%s: stream%d frame id: 0x%x\n",
					__func__,
					stream_idx, stream_info->frame_id);
				stream_info->frame_id++;

				if (stream_info->stream_type == BURST_STREAM)
					stream_info->runtime_num_burst_capture--;

				msm_isp_get_done_buf(vfe_dev, stream_info,
							pingpong_status, &done_buf);
				if (stream_info->stream_type == CONTINUOUS_STREAM ||
					stream_info->runtime_num_burst_capture > 1) {
					rc = msm_isp_cfg_ping_pong_address(vfe_dev,
						stream_info, pingpong_status);
				}
				if (done_buf && !rc)
					msm_isp_process_done_buf(vfe_dev,
					stream_info, done_buf, ts);
				if (done_buf && rc)
					/* Propagate frame drop */
					msm_isp_process_frame_drop(vfe_dev,
					stream_info, ts);
			}
		}
	}
	return;
}
