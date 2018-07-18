/****
 * Copyright (c) 2011-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mp.h"
#include "mp_internal.h"
#include <gdsync.h>
#include <gdsync/tools.h>

#if (GDS_API_MAJOR_VERSION==2 && GDS_API_MINOR_VERSION>=2) || (GDS_API_MAJOR_VERSION>2)
#define HAS_GDS_DESCRIPTOR_API 1
#endif

#define __CUDACHECK(stmt, cond_str)					\
    do {								\
        cudaError_t result = (stmt);                                    \
        if (cudaSuccess != result) {                                    \
            mp_err_msg("Assertion \"%s != cudaSuccess\" failed at %s:%d error=%d(%s)\n", \
                       cond_str, __FILE__, __LINE__, result, cudaGetErrorString(result)); \
        }                                                               \
    } while (0)

#define CUDACHECK(stmt) __CUDACHECK(stmt, #stmt)

uint32_t mp_get_lkey_from_mr(mp_reg_t *reg_t)
{
	assert(reg_t);
	struct mp_reg *reg = (struct mp_reg *)*reg_t;
	return reg->key;
}

int mp_alloc_send_info(mp_send_info_t *mp_sinfo, int mp_mem_type)
{
	int ret=0;
	if(!mp_sinfo)
	{
		mp_err_msg("!mp_sinfo\n");
		ret=EINVAL;
        goto out;
	}

	if(mp_mem_type == MP_HOSTMEM)
	{
		int page_size = sysconf(_SC_PAGESIZE);
		mp_dbg_msg("page_size=%d\n", page_size);
		mp_sinfo->ptr_to_size = (uint32_t*)memalign(page_size, 1*sizeof(uint32_t));
		//mp_sinfo->ptr_to_size = calloc(1, sizeof(uint32_t));
		memset(mp_sinfo->ptr_to_size, 0, 1*sizeof(uint32_t));
		
		mp_sinfo->ptr_to_lkey = (uint32_t*)memalign(page_size, 1*sizeof(uint32_t));
		//mp_sinfo->ptr_to_lkey = calloc(1, sizeof(uint32_t));
		memset(mp_sinfo->ptr_to_lkey, 0, 1*sizeof(uint32_t));
		
		mp_sinfo->ptr_to_addr = (uintptr_t*)memalign(page_size, 1*sizeof(uintptr_t));
		//mp_sinfo->ptr_to_addr = calloc(1, sizeof(uintptr_t));
		memset(mp_sinfo->ptr_to_addr, 0, 1*sizeof(uintptr_t));
		
		mp_sinfo->mem_type=MP_HOSTMEM;
	}
	else if(mp_mem_type == MP_HOSTMEM_PINNED)
	{
		CUDACHECK(cudaMallocHost((void**)&(mp_sinfo->ptr_to_size), 1*sizeof(uint32_t)));
		memset(mp_sinfo->ptr_to_size, 0, 1*sizeof(uint32_t));
		CUDACHECK(cudaMallocHost((void**)&(mp_sinfo->ptr_to_lkey), 1*sizeof(uint32_t)));
		memset(mp_sinfo->ptr_to_lkey, 0, 1*sizeof(uint32_t));
		CUDACHECK(cudaMallocHost((void**)&(mp_sinfo->ptr_to_addr), 1*sizeof(uintptr_t)));
		memset(mp_sinfo->ptr_to_addr, 0, 1*sizeof(uintptr_t));
		
		mp_sinfo->mem_type=MP_HOSTMEM_PINNED;
	}
	else if(mp_mem_type == MP_GPUMEM)
	{
		CUDACHECK(cudaMalloc((void**)&(mp_sinfo->ptr_to_size), 1*sizeof(uint32_t)));
		CUDACHECK(cudaMemset(mp_sinfo->ptr_to_size, 0, 1*sizeof(uint32_t))); 
		CUDACHECK(cudaMalloc((void**)&(mp_sinfo->ptr_to_lkey), 1*sizeof(uint32_t)));
		CUDACHECK(cudaMemset(mp_sinfo->ptr_to_lkey, 0, 1*sizeof(uint32_t))); 
		CUDACHECK(cudaMalloc((void**)&(mp_sinfo->ptr_to_addr), 1*sizeof(uintptr_t)));
		CUDACHECK(cudaMemset(mp_sinfo->ptr_to_addr, 0, 1*sizeof(uintptr_t))); 
		
		mp_sinfo->mem_type=MP_GPUMEM;
	}
	else
	{
		mp_err_msg("unknown memory type %x\n", mp_mem_type);
		ret=MP_FAILURE;
        goto out;
	}

	mp_dbg_msg("mp_sinfo->ptr_to_size=%p, mp_sinfo->ptr_to_lkey=%p, mp_sinfo->ptr_to_addr=%p, mem_type=%d\n", 
				mp_sinfo->ptr_to_size, mp_sinfo->ptr_to_lkey, mp_sinfo->ptr_to_addr, mp_sinfo->mem_type);
out:
	return ret;
}


int mp_dealloc_send_info(mp_send_info_t *mp_sinfo)
{
	int ret=0;
	if(!mp_sinfo)
	{
		mp_err_msg("!mp_sinfo\n");
		ret=EINVAL;
        goto out;
	}

	if(mp_sinfo->mem_type == MP_HOSTMEM)
	{
		free(mp_sinfo->ptr_to_size);
		free(mp_sinfo->ptr_to_lkey);
		free(mp_sinfo->ptr_to_addr);
	}
	else if(mp_sinfo->mem_type == MP_HOSTMEM_PINNED)
	{
		CUDACHECK(cudaFreeHost(mp_sinfo->ptr_to_size));
		CUDACHECK(cudaFreeHost(mp_sinfo->ptr_to_lkey));
		CUDACHECK(cudaFreeHost(mp_sinfo->ptr_to_addr));
	}
	else if(mp_sinfo->mem_type == MP_GPUMEM)
	{
		CUDACHECK(cudaFree(mp_sinfo->ptr_to_size));
		CUDACHECK(cudaFree(mp_sinfo->ptr_to_lkey));
		CUDACHECK(cudaFree(mp_sinfo->ptr_to_addr));
	}
	else
	{
		mp_err_msg("unknown memory type %x\n", mp_sinfo->mem_type);
		ret=MP_FAILURE;
        goto out;
	}

out:
	return ret;
}

int mp_post_send_on_stream_exp(int peer, 
								mp_request_t *req_t, 
                                cudaStream_t stream)
{
    int ret=MP_SUCCESS;
    struct mp_request *req = *req_t;
	client_t *client = &clients[client_index[peer]];

    if(!req)
        return MP_FAILURE;

    ret = gds_update_send_info(&req->gds_send_info, GDS_ASYNC, stream);
    if (ret) {
            mp_err_msg("gds_update_send_info failed: %s\n", strerror(ret));
            goto out;
    }

    ret = gds_stream_post_send(stream, &req->gds_send_info);
    if (ret) {
            mp_err_msg("gds_update_send_info failed: %s\n", strerror(ret));
            goto out;
    }

    ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
    if (ret) {
        mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
        // BUG: leaking req ??
        goto out;
    }

out:
    return ret;
}


int mp_prepare_send_exp(void *buf, int size, int peer, 
						mp_reg_t *reg_t, mp_request_t *req_t, 
						mp_send_info_t *mp_sinfo)
{
    int progress_retry=0;
    int ret = MP_SUCCESS;
    int ptr_to_size_flags=0, ptr_to_lkey_flags=0, ptr_to_addr_flags=0;
    unsigned int mem_type;
    CUresult curesult;
    const char *err_str = NULL;
    struct mp_request *req = NULL;
    struct mp_reg *reg = (struct mp_reg *)*reg_t;
    client_t *client = &clients[client_index[peer]];

    assert(mp_sinfo);

    req = new_request(client, MP_SEND, MP_PENDING_NOWAIT); //, 0);
    assert(req);

    mp_dbg_msg("req=%p id=%d\n", req, req->id);

    req->in.sr.next = NULL;
    req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
    //Useful to instruct libmlx5 to retrieve info
    req->in.sr.exp_send_flags |= IBV_EXP_SEND_GET_INFO;
    req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
    req->in.sr.wr_id = (uintptr_t) req;
    req->in.sr.num_sge = 1;
    req->in.sr.sg_list = &(req->sg_entry);

    if (mp_enable_ud) {
    	req->in.sr.wr.ud.ah = client->ah;
    	req->in.sr.wr.ud.remote_qpn = client->qpn;
    	req->in.sr.wr.ud.remote_qkey = 0;
    }

    req->sg_entry.length = size;
    req->sg_entry.lkey = reg->key;
    req->sg_entry.addr = (uintptr_t)(buf);

    ret = gds_prepare_send(client->qp, &req->in.sr, &req->out.bad_sr, &req->gds_send_info);
    if (ret) {
        mp_err_msg("mp_prepare_send failed: %s \n", strerror(ret));
        // BUG: leaking req ??
        goto out;
    }
    
	if(mp_sinfo->mem_type == MP_HOSTMEM)
	{
		ptr_to_size_flags=GDS_MEMORY_HOST;
		ptr_to_lkey_flags=GDS_MEMORY_HOST;
		ptr_to_addr_flags=GDS_MEMORY_HOST;
	}
	else if(mp_sinfo->mem_type == MP_HOSTMEM_PINNED)
	{
		ptr_to_size_flags=GDS_MEMORY_GPU;
		ptr_to_lkey_flags=GDS_MEMORY_GPU;
		ptr_to_addr_flags=GDS_MEMORY_GPU;
	}
	else if(mp_sinfo->mem_type == MP_GPUMEM)
	{
		ptr_to_size_flags=GDS_MEMORY_GPU;
		ptr_to_lkey_flags=GDS_MEMORY_GPU;
		ptr_to_addr_flags=GDS_MEMORY_GPU;
	}


	//We can avoid it with mp_sinfo
#if 0
    if(mp_sinfo != NULL && mp_sinfo->ptr_to_size != NULL)
    {
        curesult = cuPointerGetAttribute((void *)&mem_type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)mp_sinfo->ptr_to_size);
        if (curesult == CUDA_SUCCESS) {
            if (mem_type == CU_MEMORYTYPE_DEVICE) ptr_to_size_flags=GDS_MEMORY_GPU;
            else if (mem_type == CU_MEMORYTYPE_HOST) ptr_to_size_flags=GDS_MEMORY_HOST;
            else
            {
                mp_err_msg("error ptr size mem_type=%d\n", __FUNCTION__, mem_type);
                ret=MP_FAILURE;
                goto out;
            }
        }
        else {
            cuGetErrorString(ret, &err_str);        
            mp_err_msg("%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
            ret=MP_FAILURE;
            goto out;
        }        
    }

    if(mp_sinfo != NULL && mp_sinfo->ptr_to_lkey != NULL)
    {
        curesult = cuPointerGetAttribute((void *)&mem_type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)mp_sinfo->ptr_to_lkey);
        if (curesult == CUDA_SUCCESS) {
            if (mem_type == CU_MEMORYTYPE_DEVICE) ptr_to_lkey_flags=GDS_MEMORY_GPU;
            else if (mem_type == CU_MEMORYTYPE_HOST) ptr_to_lkey_flags=GDS_MEMORY_HOST;
            else
            {
                mp_err_msg("error ptr lkey mem_type=%d\n", __FUNCTION__, mem_type);
                ret=MP_FAILURE;
                goto out;
            }
        }
        else {
            cuGetErrorString(ret, &err_str);        
            mp_err_msg("%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
            ret=MP_FAILURE;
            goto out;
        }        
    }

    if(mp_sinfo != NULL && mp_sinfo->ptr_to_addr != NULL)
    {
        curesult = cuPointerGetAttribute((void *)&mem_type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)mp_sinfo->ptr_to_addr);
        if (curesult == CUDA_SUCCESS) {
            if (mem_type == CU_MEMORYTYPE_DEVICE) ptr_to_addr_flags=GDS_MEMORY_GPU;
            else if (mem_type == CU_MEMORYTYPE_HOST) ptr_to_addr_flags=GDS_MEMORY_HOST;
            else
            {
                mp_err_msg("error ptr addr mem_type=%d\n", __FUNCTION__, mem_type);
                ret=MP_FAILURE;
                goto out;
            }
        }
        else {
            cuGetErrorString(ret, &err_str);        
            mp_err_msg("%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
            ret=MP_FAILURE;
            goto out;
        }        
    }
#endif
    ret = gds_prepare_send_info(&req->gds_send_info,
                            mp_sinfo->ptr_to_size, ptr_to_size_flags,
                            mp_sinfo->ptr_to_lkey, ptr_to_lkey_flags,
                            mp_sinfo->ptr_to_addr, ptr_to_addr_flags);
    if (ret) {
            mp_err_msg("gds_prepare_send_info failed: %s\n", strerror(ret));
            goto out;
    }
    
    *req_t = req;
out:
    return ret;
}

int mp_isend_on_stream_exp(void *buf, int size, int peer, 
                            mp_reg_t *reg, mp_request_t *req, 
                            mp_send_info_t * mp_sinfo,
							cudaStream_t stream)
{
	int ret = MP_SUCCESS;

    ret = mp_prepare_send_exp(buf, size, peer, reg, req, mp_sinfo);
    if (ret) {
    	mp_err_msg("mp_post_send_on_stream failed: %s \n", strerror(ret));
        // BUG: leaking req ??
    	goto out;
    }

    ret = mp_post_send_on_stream_exp(peer, req, stream);
    if (ret) {
        mp_err_msg("mp_post_send_on_stream failed: %s \n", strerror(ret));
    // BUG: leaking req ??
        goto out;
    }

out:
    return ret;
}

/*----------------------------------------------------------------------------*/

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */