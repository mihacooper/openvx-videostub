#ifndef VX_MODULE_H
#define VX_MODULE_H

#include <initializer_list>
#include <vector>
#include "VX/vx.h"
#include "vx_debug.h"
#include "add_kernels/add_kernels.h"

struct  VideoStabParams
{
    vx_size    gauss_size;

    /*    FAST9    */
    vx_float32 fast_thresh;
    vx_uint32  fast_max_corners;
    /***************/

    /* GaussianPyramid */
    vx_float32 pyramid_scale;
    vx_size    pyramid_level;
    /*******************/

    /* GaussianPyramid */
    vx_size    optflow_wnd_size;
    vx_enum    optflow_term;
    vx_float32 optflow_estimate;
    vx_uint32  optflow_max_iter;
    /*******************/
};

class VXVideoStab
{
public:
    VXVideoStab();
    virtual ~VXVideoStab();

    vx_status CreatePipeline(const vx_uint32 width, const vx_uint32 height, VideoStabParams& params);
    vx_status EnableDebug(const std::initializer_list<vx_enum>& zones);
    vx_image  NewImage();
    vx_image  Calculate();
private:
    vx_context m_Context;
    vx_graph   m_OptFlowGraph;
    vx_graph   m_WarpGraph;
    vx_delay   m_Images;
    vx_delay   m_Matrices;
    vx_image   m_OutImage;

    vx_int32   m_NumImages;
    vx_int32   m_NumMatr;
    vx_int32   m_CurImageId;
    vx_bool    m_ImageAdded;
};

#endif // VX_MODULE_H
