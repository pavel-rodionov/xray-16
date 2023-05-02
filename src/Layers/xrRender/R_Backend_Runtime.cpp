#include "stdafx.h"
#pragma hdrstop

#if defined(USE_DX9) || defined(USE_DX11)
#include <DirectXMath.h>
#endif

void CBackend::OnFrameEnd()
{
    if (!GEnv.isDedicatedServer)
    {
#if !defined(USE_DX9) && !defined(USE_OGL)
        HW.get_context(CHW::IMM_CTX_ID)->ClearState();
#elif defined(USE_DX9)
        for (u32 stage = 0; stage < HW.Caps.raster.dwStages; stage++)
            CHK_DX(HW.pDevice->SetTexture(0, nullptr));
        CHK_DX(HW.pDevice->SetStreamSource(0, nullptr, 0, 0));
        CHK_DX(HW.pDevice->SetIndices(nullptr));
        CHK_DX(HW.pDevice->SetVertexShader(nullptr));
        CHK_DX(HW.pDevice->SetPixelShader(nullptr));
#endif
        Invalidate();
    }
}

void CBackend::OnFrameBegin()
{
    if (!GEnv.isDedicatedServer)
    {
        PGO(Msg("PGO:*****frame[%d]*****", Device.dwFrame));

#ifndef USE_DX9
        Invalidate();
        // DX9 sets base rt and base zb by default
#ifndef USE_OGL
        // XXX: Getting broken HUD hands for OpenGL after calling rmNormal()
        RImplementation.rmNormal(RCache);
#else
        set_FB(HW.pFB);
#endif
        set_RT(RImplementation.Target->get_base_rt());
        set_ZB(RImplementation.Target->get_base_zb());
#endif

        ZeroMemory(&stat, sizeof(stat));
        set_Stencil(FALSE);
    }
}

void CBackend::Invalidate()
{
    pRT[0] = 0;
    pRT[1] = 0;
    pRT[2] = 0;
    pRT[3] = 0;
    pZB = 0;
#if defined(USE_OGL)
    pFB = 0;
#endif

    decl = nullptr;
    vb = 0;
    ib = 0;
    vb_stride = 0;

    state = nullptr;
    ps = 0;
    vs = 0;
    DX11_ONLY(gs = NULL);
#ifdef USE_DX11
    hs = 0;
    ds = 0;
    cs = 0;
#endif
    ctable = nullptr;

    T = nullptr;
    M = nullptr;
    C = nullptr;

    stencil_enable = u32(-1);
    stencil_func = u32(-1);
    stencil_ref = u32(-1);
    stencil_mask = u32(-1);
    stencil_writemask = u32(-1);
    stencil_fail = u32(-1);
    stencil_pass = u32(-1);
    stencil_zfail = u32(-1);
    cull_mode = u32(-1);
    fill_mode = u32(-1);
    z_enable = u32(-1);
    z_func = u32(-1);
    alpha_ref = u32(-1);
    colorwrite_mask = u32(-1);

    // Since constant buffers are unmapped (for DirecX 10)
    // transform setting handlers should be unmapped too.
    xforms.unmap();

#if defined(USE_DX11)
    m_pInputLayout = NULL;
    m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    m_bChangedRTorZB = false;
    m_pInputSignature = NULL;
    for (int i = 0; i < MaxCBuffers; ++i)
    {
        m_aPixelConstants[i] = 0;
        m_aVertexConstants[i] = 0;
        m_aGeometryConstants[i] = 0;
        m_aHullConstants[i] = 0;
        m_aDomainConstants[i] = 0;
        m_aComputeConstants[i] = 0;
    }
    StateManager.Reset();
    // Redundant call. Just no note that we need to unmap const
    // if we create dedicated class.
    StateManager.UnmapConstants();
    SRVSManager.ResetDeviceState();

    for (u32 gs_it = 0; gs_it < CTexture::mtMaxGeometryShaderTextures;)
        textures_gs[gs_it++] = 0;
    for (u32 hs_it = 0; hs_it < CTexture::mtMaxHullShaderTextures;)
        textures_hs[hs_it++] = 0;
    for (u32 ds_it = 0; ds_it < CTexture::mtMaxDomainShaderTextures;)
        textures_ds[ds_it++] = 0;
    for (u32 cs_it = 0; cs_it < CTexture::mtMaxComputeShaderTextures;)
        textures_cs[cs_it++] = 0;

    context_id = CHW::IMM_CTX_ID;
#endif // USE_DX11

    for (u32 ps_it = 0; ps_it < CTexture::mtMaxPixelShaderTextures;)
        textures_ps[ps_it++] = nullptr;
    for (u32 vs_it = 0; vs_it < CTexture::mtMaxVertexShaderTextures;)
        textures_vs[vs_it++] = nullptr;
    for (auto& matrix : matrices)
        matrix = nullptr;
}

void CBackend::set_ClipPlanes(u32 _enable, Fplane* _planes /*=NULL */, u32 count /* =0*/)
{
#if defined(USE_DX9)
    if (0 == HW.Caps.geometry.dwClipPlanes)
        return;
    if (!_enable)
    {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, FALSE));
        return;
    }

    // Enable and setup planes
    VERIFY(_planes && count);
    if (count > HW.Caps.geometry.dwClipPlanes)
        count = HW.Caps.geometry.dwClipPlanes;

    using namespace DirectX;

    const XMMATRIX transform = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&Device.mFullTransform));
    XMMATRIX worldToClipMatrixIT = XMMatrixInverse(nullptr, transform);
    worldToClipMatrixIT = XMMatrixTranspose(worldToClipMatrixIT);

    for (u32 it = 0; it < count; it++)
    {
        Fplane& P = _planes[it];
        XMFLOAT4 planeClip;
        XMVECTOR planeWorld = XMPlaneNormalize(XMVectorSet(-P.n.x, -P.n.y, -P.n.z, -P.d));
        XMStoreFloat4(&planeClip, XMPlaneTransform(planeWorld, worldToClipMatrixIT));
        CHK_DX(HW.pDevice->SetClipPlane(it, reinterpret_cast<float*>(&planeClip)));
    }

    // Enable them
    u32 e_mask = (1 << count) - 1;
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, e_mask));	
#elif defined(USE_DX11) || defined(USE_OGL)
    // TODO: DX11: Implement in the corresponding vertex shaders
    // Use this to set up location, were shader setup code will get data
    // VERIFY(!"CBackend::set_ClipPlanes not implemented!");
    UNUSED(_enable);
    UNUSED(_planes);
    UNUSED(count);
    return;
#else
#   error No graphics API selected or enabled!
#endif //USE_DX9
}

#ifndef DEDICATED_SREVER
void CBackend::set_ClipPlanes(u32 _enable, Fmatrix* _xform /*=NULL */, u32 fmask /* =0xff */)
{
    if (0 == HW.Caps.geometry.dwClipPlanes)
        return;
    if (!_enable)
    {
#if defined(USE_DX9)
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, FALSE));
#elif defined(USE_DX11) || defined(USE_OGL)
    // TODO: DX11: Implement in the corresponding vertex shaders
    // Use this to set up location, were shader setup code will get data
    // VERIFY(!"CBackend::set_ClipPlanes not implemented!");
#else
#   error No graphics API selected or enabled!
#endif
        return;
    }
    VERIFY(_xform && fmask);
    CFrustum F;
    F.CreateFromMatrix(*_xform, fmask);
    set_ClipPlanes(_enable, F.planes, F.p_count);
}

void CBackend::set_Textures(STextureList* _T)
{
    if (T == _T)
        return;
    T = _T;
    // If resources weren't set at all we should clear from resource #0.
    int _last_ps = -1;
    int _last_vs = -1;
#if defined(USE_DX11)
    int _last_gs = -1;
    int _last_hs = -1;
    int _last_ds = -1;
    int _last_cs = -1;
#endif
    STextureList::iterator _it = _T->begin();
    STextureList::iterator _end = _T->end();

    for (; _it != _end; ++_it)
    {
        std::pair<u32, ref_texture>& loader = *_it;
        u32 load_id = loader.first;
        CTexture* load_surf = loader.second._get();
        //if (load_id < 256) {
        if (load_id < CTexture::rstVertex)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::mtMaxPixelShaderTextures);
            // ordinary pixel surface
            if ((int)load_id > _last_ps)
                _last_ps = load_id;
            if (textures_ps[load_id] != load_surf)
            {
                textures_ps[load_id] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
        else
#if defined(USE_DX11)
        if (load_id < CTexture::rstGeometry)
#endif
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstVertex + CTexture::mtMaxVertexShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstVertex;
            if ((int)load_id_remapped > _last_vs)
                _last_vs = load_id_remapped;
            if (textures_vs[load_id_remapped] != load_surf)
            {
                textures_vs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
#if defined(USE_DX11)
        else if (load_id < CTexture::rstHull)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstGeometry + CTexture::mtMaxGeometryShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstGeometry;
            if ((int)load_id_remapped > _last_gs)
                _last_gs = load_id_remapped;
            if (textures_gs[load_id_remapped] != load_surf)
            {
                textures_gs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
        else if (load_id < CTexture::rstDomain)
        {
            //  Set up pixel shader resources
            VERIFY(load_id < CTexture::rstHull + CTexture::mtMaxHullShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstHull;
            if ((int)load_id_remapped > _last_hs)
                _last_hs = load_id_remapped;
            if (textures_hs[load_id_remapped] != load_surf)
            {
                textures_hs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
        else if (load_id < CTexture::rstCompute)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstDomain + CTexture::mtMaxDomainShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstDomain;
            if ((int)load_id_remapped > _last_ds)
                _last_ds = load_id_remapped;
            if (textures_ds[load_id_remapped] != load_surf)
            {
                textures_ds[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
        else if (load_id < CTexture::rstInvalid)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstCompute + CTexture::mtMaxComputeShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstCompute;
            if ((int)load_id_remapped > _last_cs)
                _last_cs = load_id_remapped;
            if (textures_cs[load_id_remapped] != load_surf)
            {
                textures_cs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(load_id);
                    //load_surf->Applyload_id);
                }
            }
        }
        else
        {
            VERIFY("Invalid enum");
        }
#endif // USE_DX11
    }

    // clear remaining stages (PS)
    for (++_last_ps; _last_ps < CTexture::mtMaxPixelShaderTextures; _last_ps++)
    {
        if (!textures_ps[_last_ps])
            continue;

        textures_ps[_last_ps] = nullptr;
#if defined(USE_DX9)
        CHK_DX(HW.pDevice->SetTexture(_last_ps, NULL));
#elif defined(USE_DX11)
        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        // HW.pDevice->PSSetShaderResources(_last_ps, 1, &pRes);
        SRVSManager.SetPSResource(_last_ps, pRes);
#elif defined(USE_OGL)
        CHK_GL(glActiveTexture(GL_TEXTURE0 + _last_ps));
        CHK_GL(glBindTexture(GL_TEXTURE_2D, 0));
        if (RImplementation.o.msaa)
            CHK_GL(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_3D, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
#else
#   error No graphics API selected or enabled!
#endif
    }
    // clear remaining stages (VS)
    for (++_last_vs; _last_vs < CTexture::mtMaxVertexShaderTextures; _last_vs++)
    {
        if (!textures_vs[_last_vs])
            continue;

        textures_vs[_last_vs] = nullptr;
#if defined(USE_DX9)
        CHK_DX(HW.pDevice->SetTexture(_last_vs + CTexture::rstVertex, NULL));
#elif defined(USE_DX11)
        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        // HW.pDevice->VSSetShaderResources(_last_vs, 1, &pRes);
        SRVSManager.SetVSResource(_last_vs, pRes);
#elif defined(USE_OGL)
        CHK_GL(glActiveTexture(GL_TEXTURE0 + CTexture::rstVertex + _last_vs));
        CHK_GL(glBindTexture(GL_TEXTURE_2D, 0));
        if (RImplementation.o.msaa)
            CHK_GL(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_3D, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
#else
#   error No graphics API selected or enabled!
#endif
    }

#if defined(USE_DX11)
    // clear remaining stages (VS)
    for (++_last_gs; _last_gs < CTexture::mtMaxGeometryShaderTextures; _last_gs++)
    {
        if (!textures_gs[_last_gs])
            continue;

        textures_gs[_last_gs] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        // HW.pDevice->GSSetShaderResources(_last_gs, 1, &pRes);
        SRVSManager.SetGSResource(_last_gs, pRes);
    }
    for (++_last_hs; _last_hs < CTexture::mtMaxHullShaderTextures; _last_hs++)
    {
        if (!textures_hs[_last_hs])
            continue;

        textures_hs[_last_hs] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        SRVSManager.SetHSResource(_last_hs, pRes);
    }
    for (++_last_ds; _last_ds < CTexture::mtMaxDomainShaderTextures; _last_ds++)
    {
        if (!textures_ds[_last_ds])
            continue;

        textures_ds[_last_ds] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        SRVSManager.SetDSResource(_last_ds, pRes);
    }
    for (++_last_cs; _last_cs < CTexture::mtMaxComputeShaderTextures; _last_cs++)
    {
        if (!textures_cs[_last_cs])
            continue;

        textures_cs[_last_cs] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        SRVSManager.SetCSResource(_last_cs, pRes);
    }
#endif // USE_DX11
}
#else

void CBackend::set_ClipPlanes(u32 _enable, Fmatrix* _xform /*=NULL */, u32 fmask /* =0xff */) {}
void CBackend::set_Textures(STextureList* _T) {}

#endif // DEDICATED SERVER

void CBackend::SetupStates()
{
    set_CullMode(CULL_CCW);
#if defined(USE_DX9)
    for (u32 i = 0; i < HW.Caps.raster.dwStages; i++)
    {
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, ps_r__tf_Anisotropic));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, *(u32*)&ps_r__tf_Mipbias));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
    }
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_DITHERENABLE, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_COLORVERTEX, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_ZENABLE, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_LOCALVIEWER, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_COLOR1));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE));

    Fmaterial mat
    {
        /*.diffuse  =*/ { 1, 1, 1, 1 },
        /*.ambient  =*/ { 1, 1, 1, 1 },
        /*.emissive =*/ { 0, 0, 0, 0 },
        /*.specular =*/ { 1, 1, 1, 1 },
        /*.power    =*/ 15.f
    };
    CHK_DX(HW.pDevice->SetMaterial(reinterpret_cast<D3DMATERIAL9*>(&mat)));

    if (psDeviceFlags.test(rsWireframe))
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME));
    else
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
    // ******************** Fog parameters
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGCOLOR, 0));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE));
    if (HW.Caps.bTableFog)
    {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE));
    }
    else
    {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR));
    }
#elif defined(USE_DX11)
    SSManager.SetMaxAnisotropy(ps_r__tf_Anisotropic);
    SSManager.SetMipLODBias(ps_r__tf_Mipbias);
#elif defined(USE_OGL)
    // TODO: OGL: Implement SetupStates().
#else
#   error No graphics API selected or enabled!
#endif
}


// Device dependance
void CBackend::OnDeviceCreate()
{
    // Debug Draw
    InitializeDebugDraw();

    // invalidate caching
    Invalidate();
}

void CBackend::OnDeviceDestroy()
{
    // Debug Draw
    DestroyDebugDraw();

#if defined(USE_DX11)
    //  Destroy state managers
    StateManager.Reset();
    RSManager.ClearStateArray();
    DSSManager.ClearStateArray();
    BSManager.ClearStateArray();
    SSManager.ClearStateArray();
#endif
}

#include "LightTrack.h"
#include "xrEngine/IRenderable.h"

void CBackend::apply_lmaterial()
{
    R_constant* C = get_c(c_sbase)._get(); // get sampler
    if (!C)
        return;

    VERIFY(RC_dest_sampler == C->destination);
#if defined(USE_DX9)
    VERIFY(RC_sampler == C->type);
#elif defined(USE_DX11)
    VERIFY(RC_dx11texture == C->type);
#elif defined(USE_OGL)
    VERIFY(RC_sampler == C->type);
#else
#   error No graphics API selected or enabled!
#endif

    CTexture* T = get_ActiveTexture(u32(C->samp.index));
    VERIFY(T);
    float mtl = T->m_material;
#ifdef DEBUG
    if (ps_r2_ls_flags.test(R2FLAG_GLOBALMATERIAL))
        mtl = ps_r2_gmaterial;
#endif
    hemi.set_material(o_hemi, o_sun, 0, (mtl + .5f) / 4.f);
    hemi.set_pos_faces(o_hemi_cube[CROS_impl::CUBE_FACE_POS_X],
                                o_hemi_cube[CROS_impl::CUBE_FACE_POS_Y],
                                o_hemi_cube[CROS_impl::CUBE_FACE_POS_Z]);
    hemi.set_neg_faces(o_hemi_cube[CROS_impl::CUBE_FACE_NEG_X],
                                o_hemi_cube[CROS_impl::CUBE_FACE_NEG_Y],
                                o_hemi_cube[CROS_impl::CUBE_FACE_NEG_Z]);
}
