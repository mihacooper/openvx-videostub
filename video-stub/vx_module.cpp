#include "vx_module.h"
#include "math.h"
#include "memory.h"
#include <cstdio>


#define INIT_DEBUG(zones, num) \
    { \
        for(int i = 0; i < (num); i++) \
            vx_set_debug_zone((zones)[i]); \
    }

#define CHECK_NULL(var) \
    { \
        if( (var) == NULL ) \
        {\
            VX_PRINT(VX_ZONE_ERROR, "NULL reference of " #var "\n"); \
            return VX_FAILURE; \
        } \
    }

#define CHECK_STATUS(var) \
    { \
        if( (var) != VX_SUCCESS ) \
        { \
            VX_PRINT(VX_ZONE_ERROR, #var "return bad status\n"); \
            return VX_FAILURE; \
        } \
    }

inline vx_int32 min(vx_int32 left, vx_int32 right)
{
    return left < right ? left : right;
}

inline vx_int32 max(vx_int32 left, vx_int32 right)
{
    return left > right ? left : right;
}

VXVideoStub::VXVideoStub() :
    m_CurImageId(0), m_NumImages(0), m_Images(NULL),
    m_Matrices(NULL), m_OptFlowGraph(NULL), m_WarpGraph(NULL), m_ImageAdded(vx_false_e)
{
    m_Context = vxCreateContext();
    if(m_Context == NULL)
    {
        VX_PRINT(VX_ZONE_ERROR, "Can't create VX context. Stop.");
    }
}

VXVideoStub::~VXVideoStub()
{
    vxReleaseContext(&m_Context);
}

vx_status VXVideoStub::EnableDebug(const std::initializer_list<vx_enum>& zones)
{
    for(auto i = zones.begin(); i != zones.end(); i++) \
        vx_set_debug_zone(*i);
}

#define MAX_PYRAMID_LEVELS 10
#define MAX_FOUNDED_CORNERS 200

vx_status VXVideoStub::CreatePipeline(const vx_uint32 width, const vx_uint32 height, const vx_uint32 gauss_size)
{
    if(m_Context == NULL)
        return VX_FAILURE;

    int i;
    m_NumImages = gauss_size;
    /*****FAST9 params*****/
    vx_float32 fast_thresh = 100.f; // threshold parameter of FAST9
    vx_uint32  corners_num = 100;    // tmp value for init scalar
    /*****OptFlow params*****/
    vx_size optflow_wnd_size = 10;
    vx_float32 pyramid_scale = VX_SCALE_PYRAMID_HALF;
    vx_size    pyramid_level = min(
                floor(log(vx_float32(optflow_wnd_size) / vx_float32(width)) / log(pyramid_scale)),
                floor(log(vx_float32(optflow_wnd_size) / vx_float32(height)) / log(pyramid_scale))
                );
    pyramid_level = max(1, min(pyramid_level, MAX_PYRAMID_LEVELS));
    vx_enum optflow_term = VX_TERM_CRITERIA_BOTH;
    vx_float32 optflow_estimate = 0.1;
    vx_uint32 optflow_max_iter = 500;
    vx_uint32 optflow_init_estimate = vx_false_e;

    vx_float32 sigma = (m_NumImages - 2) * 0.35;
    vx_float32 matr_coeffs[4];
    for (i = -1; i <= 1; ++i)
        matr_coeffs[i + 1] = ( exp(-i * i / (2.f * sigma * sigma)) );
    vx_float32 sum = 0.;
    for (i = 0; i < 3; ++i)
        sum += matr_coeffs[i];
    sum = 1. / sum;
    for (i = 0; i < 3; ++i)
        matr_coeffs[i] *= sum;
    /******End of params******/

    m_OptFlowGraph = vxCreateGraph(m_Context);
    CHECK_NULL(m_OptFlowGraph);

    /***     Create images   ***/
    vx_image tmp_image = vxCreateImage(m_Context, width, height, VX_DF_IMAGE_RGB);
    m_Images = vxCreateDelay(m_Context, (vx_reference)tmp_image, gauss_size);
    vx_image gray_image_1 = vxCreateVirtualImage(m_OptFlowGraph, width, height, VX_DF_IMAGE_U8);
    vx_image gray_image_2 = vxCreateVirtualImage(m_OptFlowGraph, width, height, VX_DF_IMAGE_U8);
    /***     Check images    ***/
    CHECK_NULL(m_Images);
    CHECK_NULL(gray_image_1);
    CHECK_NULL(gray_image_2);
    /***    End of images    ***/

    /***    Create objects    ***/
    vx_matrix tmp_matr = vxCreateMatrix(m_Context, VX_TYPE_FLOAT32, 3, 3);
    m_Matrices = vxCreateDelay(m_Context, (vx_reference)tmp_matr, gauss_size - 1);
    vx_scalar  fast_thresh_s     = vxCreateScalar(m_Context, VX_TYPE_FLOAT32, &fast_thresh);
    vx_scalar  fast_num_corn_s   = vxCreateScalar(m_Context, VX_TYPE_UINT32, &corners_num);
    vx_array   fast_found_corn_s = vxCreateArray(m_Context, VX_TYPE_KEYPOINT, MAX_FOUNDED_CORNERS);
    vx_array   optf_moved_corn_s = vxCreateArray(m_Context, VX_TYPE_KEYPOINT, MAX_FOUNDED_CORNERS);
    vx_array   optf_extim_corn_s = vxCreateArray(m_Context, VX_TYPE_KEYPOINT, MAX_FOUNDED_CORNERS);
    vx_scalar  optf_estimate_s   = vxCreateScalar(m_Context, VX_TYPE_FLOAT32, &optflow_estimate);
    vx_scalar  optf_max_iter_s   = vxCreateScalar(m_Context, VX_TYPE_UINT32, &optflow_max_iter);
    vx_scalar  optf_init_estim   = vxCreateScalar(m_Context, VX_TYPE_BOOL, &optflow_init_estimate);
    vx_pyramid pyramid_1         = vxCreatePyramid(m_Context, pyramid_level, pyramid_scale, width, height, VX_DF_IMAGE_U8);
    vx_pyramid pyramid_2         = vxCreatePyramid(m_Context, pyramid_level, pyramid_scale, width, height, VX_DF_IMAGE_U8);
    /***      Check objects   ***/
    CHECK_NULL(fast_thresh_s);
    CHECK_NULL(fast_num_corn_s);
    CHECK_NULL(fast_found_corn_s);
    CHECK_NULL(optf_moved_corn_s);
    CHECK_NULL(optf_extim_corn_s);
    CHECK_NULL(pyramid_1);
    CHECK_NULL(pyramid_2);
    /***    End of objects    ***/

    CHECK_NULL( vxRGBtoGrayNode(m_OptFlowGraph, (vx_image)vxGetReferenceFromDelay(m_Images, 1 ), gray_image_1) );
    CHECK_NULL( vxRGBtoGrayNode(m_OptFlowGraph, (vx_image)vxGetReferenceFromDelay(m_Images, 0 ), gray_image_2) );
    CHECK_NULL( vxFastCornersNode(m_OptFlowGraph, gray_image_1, fast_thresh_s, vx_false_e, fast_found_corn_s, fast_num_corn_s) );

    CHECK_NULL( vxGaussianPyramidNode(m_OptFlowGraph, gray_image_1, pyramid_1) );
    CHECK_NULL( vxGaussianPyramidNode(m_OptFlowGraph, gray_image_2, pyramid_2) );
    CHECK_NULL( vxOpticalFlowPyrLKNode(m_OptFlowGraph, pyramid_1, pyramid_2, fast_found_corn_s,
                                       optf_extim_corn_s, optf_moved_corn_s, optflow_term,
                                       optf_estimate_s, optf_max_iter_s, optf_init_estim, optflow_wnd_size) );
    CHECK_NULL( vxFindWarpNode(m_OptFlowGraph, fast_found_corn_s, optf_moved_corn_s, (vx_matrix)vxGetReferenceFromDelay(m_Matrices, 0 )) );

    CHECK_STATUS( vxVerifyGraph(m_OptFlowGraph) );

    /*** Warp Graph***/
    m_WarpGraph = vxCreateGraph(m_Context);
    CHECK_NULL(m_WarpGraph);
    m_OutImage = vxCreateImage(m_Context, width, height, VX_DF_IMAGE_RGB);
    vx_matrix prev_matr = (vx_matrix)vxGetReferenceFromDelay(m_Matrices, 0);
    for(i = 1; i < m_NumImages - 1; i++)
    {
        vx_scalar coeff_s = vxCreateScalar(m_Context, VX_TYPE_FLOAT32, &matr_coeffs[i - 1]);
        vx_matrix next_matr = vxCreateMatrix(m_Context, VX_TYPE_FLOAT32, 3, 3);
        CHECK_NULL(vxMatrixMultiplyNode(m_WarpGraph, prev_matr, (vx_matrix)vxGetReferenceFromDelay(m_Matrices, i ), coeff_s, next_matr));
        prev_matr = next_matr;
    }
    vx_enum inter = VX_INTERPOLATION_TYPE_NEAREST_NEIGHBOR;
    vx_scalar inter_s = vxCreateScalar(m_Context, VX_TYPE_ENUM, &inter);
    vx_node warp_node = vxWarpPerspectiveRGBNode(m_WarpGraph, (vx_image)vxGetReferenceFromDelay(m_Images, m_NumImages / 2 ),
                                              prev_matr, inter_s, m_OutImage);
    vx_border_mode_t border = {VX_BORDER_MODE_CONSTANT, 0};
    vxSetNodeAttribute(warp_node, VX_NODE_ATTRIBUTE_BORDER_MODE, &border, sizeof(border));
    CHECK_STATUS( vxVerifyGraph(m_WarpGraph) );
    /*****************/
}

vx_image VXVideoStub::NewImage()
{
    if(m_Images == NULL)
    {
        VX_PRINT(VX_ZONE_ERROR, "Pipeline wasn't created!\n");
        return NULL;
    }

    if(m_CurImageId < m_NumImages)
    {
        m_ImageAdded = vx_true_e;
        vx_image ret = (vx_image)vxGetReferenceFromDelay(m_Images, 0);
        m_CurImageId++;
        return ret;
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "Can't store more image, need calculate!\n");
        return NULL;
    }
}

vx_image VXVideoStub::Calculate()
{
    if(!m_ImageAdded)
    {
        VX_PRINT(VX_ZONE_WARNING, "Add new image first!\n");
        return NULL;
    }

    if(m_CurImageId > 1)
    {
        if(vxProcessGraph(m_OptFlowGraph) != VX_SUCCESS)
        {
            VX_PRINT(VX_ZONE_ERROR, "Optical flow graph process error!\n");
            return NULL;
        }
    }
    vx_image ret = NULL;
    if(m_CurImageId == m_NumImages)
    {
        if(vxProcessGraph(m_WarpGraph) != VX_SUCCESS)
        {
            VX_PRINT(VX_ZONE_ERROR, "Warp graph process error!\n");
            return NULL;
        }
        ret = m_OutImage;
        m_CurImageId--;
    }
    vxAgeDelay(m_Images);
    vxAgeDelay(m_Matrices);
    m_ImageAdded = vx_false_e;
    return ret;
}

