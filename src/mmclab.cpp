/***************************************************************************//**
**  \mainpage Mesh-based Monte Carlo (MMC) - a 3D photon simulator
**
**  \author Qianqian Fang <q.fang at neu.edu>
**  \copyright Qianqian Fang, 2010-2025
**
**  \section sref Reference:
**  \li \c (\b Fang2010) Qianqian Fang, <a href="http://www.opticsinfobase.org/abstract.cfm?uri=boe-1-1-165">
**          "Mesh-based Monte Carlo Method Using Fast Ray-Tracing
**          in Plucker Coordinates,"</a> Biomed. Opt. Express, 1(1) 165-175 (2010).
**  \li \c (\b Fang2012) Qianqian Fang and David R. Kaeli,
**           <a href="https://www.osapublishing.org/boe/abstract.cfm?uri=boe-3-12-3223">
**          "Accelerating mesh-based Monte Carlo method on modern CPU architectures,"</a>
**          Biomed. Opt. Express 3(12), 3223-3230 (2012)
**  \li \c (\b Yao2016) Ruoyang Yao, Xavier Intes, and Qianqian Fang,
**          <a href="https://www.osapublishing.org/boe/abstract.cfm?uri=boe-7-1-171">
**          "Generalized mesh-based Monte Carlo for wide-field illumination and detection
**           via mesh retessellation,"</a> Biomed. Optics Express, 7(1), 171-184 (2016)
**  \li \c (\b Fang2019) Qianqian Fang and Shijie Yan,
**          <a href="http://dx.doi.org/10.1117/1.JBO.24.11.115002">
**          "Graphics processing unit-accelerated mesh-based Monte Carlo photon transport
**           simulations,"</a> J. of Biomedical Optics, 24(11), 115002 (2019)
**  \li \c (\b Yuan2021) Yaoshen Yuan, Shijie Yan, and Qianqian Fang,
**          <a href="https://www.osapublishing.org/boe/fulltext.cfm?uri=boe-12-1-147">
**          "Light transport modeling in highly complex tissues using the implicit
**           mesh-based Monte Carlo algorithm,"</a> Biomed. Optics Express, 12(1) 147-161 (2021)
**
**  \section slicense License
**          GPL v3, see LICENSE.txt for details
*******************************************************************************/

/***************************************************************************//**
\file    mmclab.cpp

@brief   mex function for MMCLAB
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <exception>

#ifdef _OPENMP
    #include <omp.h>
#endif

#include "mex.h"
#include "mmc_const.h"
#include "mmc_mesh.h"
#include "mmc_host.h"
#ifdef USE_OPENCL
    #include "mmc_cl_host.h"
#endif
#ifdef USE_CUDA
    #include "mmc_cu_host.h"
#endif
#include "mmc_tictoc.h"
#include "mmc_raytrace.h"

//! Macro to read the 1st scalar cfg member
#define GET_1ST_FIELD(x,y)  if(strcmp(name,#y)==0) {double *val=mxGetPr(item);x->y=val[0];printf("mmc.%s=%g;\n",#y,(float)(x->y));}

//! Macro to read one scalar cfg member
#define GET_ONE_FIELD(x,y)  else GET_1ST_FIELD(x,y)

//! Macro to read one 3-element vector member of cfg
#define GET_VEC3_FIELD(u,v) else if(strcmp(name,#v)==0) {double *val=mxGetPr(item);u->v.x=val[0];u->v.y=val[1];u->v.z=val[2];\
        printf("mmc.%s=[%g %g %g];\n",#v,(float)(u->v.x),(float)(u->v.y),(float)(u->v.z));}

//! Macro to read one 3- or 4-element vector member of cfg
#define GET_VEC34_FIELD(u,v) else if(strcmp(name,#v)==0) {double *val=mxGetPr(item);u->v.x=val[0];u->v.y=val[1];u->v.z=val[2];if(mxGetNumberOfElements(item)==4) u->v.w=val[3];\
        printf("mmc.%s=[%g %g %g %g];\n",#v,(float)(u->v.x),(float)(u->v.y),(float)(u->v.z),(float)(u->v.w));}

//! Macro to read one 4-element vector member of cfg
#define GET_VEC4_FIELD(u,v) else if(strcmp(name,#v)==0) {double *val=mxGetPr(item);u->v.x=val[0];u->v.y=val[1];u->v.z=val[2];u->v.w=val[3];\
        printf("mmc.%s=[%g %g %g %g];\n",#v,(float)(u->v.x),(float)(u->v.y),(float)(u->v.z),(float)(u->v.w));}
/**<  Macro to output GPU parameters as field */
#define SET_GPU_INFO(output,id,v)  mxSetField(output,id,#v,mxCreateDoubleScalar(gpuinfo[i].v));

#define MEXERROR(a)  mcx_error(999,a,__FILE__,__LINE__)   //! Macro to add unit name and line number in error printing

typedef mwSize dimtype;                                   //! MATLAB type alias for integer type to use for array sizes and dimensions

void mmc_set_field(const mxArray* root, const mxArray* item, int idx, mcconfig* cfg, tetmesh* mesh);
void mmclab_usage();

extern const char debugflag[];

float* detps = NULL;       //! buffer to receive data from cfg.detphotons field
int    dimdetps[2] = {0, 0}; //! dimensions of the cfg.detphotons array
int    seedbyte = 0;

/** @brief Mex function for the MMC host function for MATLAB/Octave
 *  This is the master function to interface all MMC features inside MATLAB.
 *  In MMCLAB, all inputs are read from the cfg structure, which contains all
 *  simuation parameters and data.
 */

void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    mcconfig cfg;
    tetmesh mesh;
    raytracer tracer = {NULL, 0, NULL, NULL, NULL};
    unsigned int threadid = 0, t0, dt;
    GPUInfo* gpuinfo = NULL;

    mxArray*    tmp;
    int        ifield, jstruct;
    int        ncfg, nfields;
    dimtype     fielddim[5];
    int        errorflag = 0;
    cl_uint    workdev;

    const char*       outputtag[] = {"data"};
    const char*       datastruct[] = {"data", "dref"};
    const char*       gpuinfotag[] = {"name", "id", "devcount", "major", "minor", "globalmem",
                                      "constmem", "sharedmem", "regcount", "clock", "sm", "core",
                                      "autoblock", "autothread", "maxgate"
                                     };

    /**
     * If no input is given for this function, it prints help information and return.
     */
    if (nrhs == 0) {
        mmclab_usage();
        return;
    }

    /**
     * If a single string is passed, and if this string is 'gpuinfo', this function
     * returns the list of GPUs on this host and return.
     */
    if (nrhs == 1 && mxIsChar(prhs[0])) {
        char shortcmd[MAX_SESSION_LENGTH];
        mxGetString(prhs[0], shortcmd, MAX_SESSION_LENGTH);
        shortcmd[MAX_SESSION_LENGTH - 1] = '\0';

        if (strcmp(shortcmd, "gpuinfo") == 0) {
            mcx_initcfg(&cfg);
            cfg.isgpuinfo = 3;

            try {
                mcx_list_cl_gpu(&cfg, &workdev, NULL, &gpuinfo);
            } catch (...) {
                mexErrMsgTxt("OpenCL is not supported or not fully installed on your system");
            }

            if (!workdev) {
                mexErrMsgTxt("no active GPU device found");
            }

            if (workdev > MAX_DEVICE) {
                workdev = MAX_DEVICE;
            }

            plhs[0] = mxCreateStructMatrix(gpuinfo[0].devcount, 1, 15, gpuinfotag);

            for (cl_uint i = 0; i < workdev; i++) {
                mxSetField(plhs[0], i, "name", mxCreateString(gpuinfo[i].name));
                SET_GPU_INFO(plhs[0], i, id);
                SET_GPU_INFO(plhs[0], i, devcount);
                SET_GPU_INFO(plhs[0], i, major);
                SET_GPU_INFO(plhs[0], i, minor);
                SET_GPU_INFO(plhs[0], i, globalmem);
                SET_GPU_INFO(plhs[0], i, constmem);
                SET_GPU_INFO(plhs[0], i, sharedmem);
                SET_GPU_INFO(plhs[0], i, regcount);
                SET_GPU_INFO(plhs[0], i, clock);
                SET_GPU_INFO(plhs[0], i, sm);
                SET_GPU_INFO(plhs[0], i, core);
                SET_GPU_INFO(plhs[0], i, autoblock);
                SET_GPU_INFO(plhs[0], i, autothread);
                SET_GPU_INFO(plhs[0], i, maxgate);
            }

            mcx_cleargpuinfo(&gpuinfo);
            mcx_clearcfg(&cfg);
        }

        return;
    }

    /**
     * If a structure is passed to this function, a simulation will be launched.
     */
    printf("Launching MMCLAB - Mesh-based Monte Carlo for MATLAB & GNU Octave ...\n");

    if (!mxIsStruct(prhs[0])) {
        MEXERROR("Input must be a structure.");
    }

    /**
     * Find out information about input and output.
     */
    nfields = mxGetNumberOfFields(prhs[0]);
    ncfg = mxGetNumberOfElements(prhs[0]);

    /**
     * The function can return 1-3 outputs (i.e. the LHS)
     */
    if (nlhs >= 1) {
        plhs[0] = mxCreateStructMatrix(ncfg, 1, 2, datastruct);
    }

    if (nlhs >= 2) {
        plhs[1] = mxCreateStructMatrix(ncfg, 1, 1, outputtag);
    }

    if (nlhs >= 3) {
        plhs[2] = mxCreateStructMatrix(ncfg, 1, 1, outputtag);
    }

    if (nlhs >= 4) {
        plhs[3] = mxCreateStructMatrix(ncfg, 1, 1, outputtag);
    }

    /**
     * Loop over each element of the struct if it is an array, each element is a simulation
     */
    for (jstruct = 0; jstruct < ncfg; jstruct++) {  /* how many configs */

        /** Enclose all simulation calls inside a try/catch construct for exception handling */
        try {
            printf("Running simulations for configuration #%d ...\n", jstruct + 1);

            /** Initialize cfg with default values first */
            t0 = StartTimer();
            mcx_initcfg(&cfg);
            MMCDEBUG(&cfg, dlTime, (cfg.flog, "initializing ... "));
            mesh_init(&mesh);

            /** Read each struct element from input and set value to the cfg configuration */
            for (ifield = 0; ifield < nfields; ifield++) { /* how many input struct fields */
                tmp = mxGetFieldByNumber(prhs[0], jstruct, ifield);

                if (tmp == NULL) {
                    continue;
                }

                mmc_set_field(prhs[0], tmp, ifield, &cfg, &mesh);
            }

            mexEvalString("pause(.001);");

            /** Overwite the output flags using the number of output present */
            cfg.issave2pt = (nlhs >= 1); /** save fluence rate to the 1st output if present */
            cfg.issavedet = (nlhs >= 2); /** save detected photon data to the 2nd output if present */
            cfg.issaveseed = (nlhs >= 3); /** save detected photon seeds to the 3rd output if present */

            if (nlhs >= 4) {
                cfg.exportdebugdata = (float*)malloc(cfg.maxjumpdebug * sizeof(float) * MCX_DEBUG_REC_LEN);
                cfg.debuglevel |= dlTraj;
            }

#if defined(MMC_LOGISTIC) || defined(MMC_SFMT)
            cfg.issaveseed = 0;
#endif
            mesh_srcdetelem(&mesh, &cfg);

            /** Validate all input fields, and warn incompatible inputs */
            mmc_validate_config(&cfg, detps, dimdetps, seedbyte);
            mesh_validate(&mesh, &cfg);

            if (cfg.isgpuinfo == 0) {
                mmc_prep(&cfg, &mesh, &tracer);
            }

            dt = GetTimeMillis();
            MMCDEBUG(&cfg, dlTime, (cfg.flog, "\tdone\t%d\nsimulating ... \n", dt - t0));


            /** \subsection ssimu Parallel photon transport simulation */

            try {
                if (cfg.compute == cbSSE || cfg.gpuid > MAX_DEVICE) {
                    mmc_run_mp(&cfg, &mesh, &tracer);
                }

#ifdef USE_CUDA
                else if (cfg.compute == cbCUDA) {
                    mmc_run_cu(&cfg, &mesh, &tracer);
                }

#endif
#ifdef USE_OPENCL
                else {
                    mmc_run_cl(&cfg, &mesh, &tracer);
                }

#endif
            } catch (const char* err) {
                mexPrintf("Error from thread (%d): %s\n", threadid, err);
                errorflag++;
            } catch (const std::exception& err) {
                mexPrintf("C++ Error from thread (%d): %s\n", threadid, err.what());
                errorflag++;
            } catch (...) {
                mexPrintf("Unknown Exception from thread (%d)", threadid);
                errorflag++;
            }

            /** \subsection sreport Post simulation */

            dt = GetTimeMillis() - dt;

            /** Clear up simulation data structures by calling the destructors */

            tracer_clear(&tracer);
            MMCDEBUG(&cfg, dlTime, (cfg.flog, "\tdone\t%d\n", GetTimeMillis() - t0));

            /** if 5th output presents, output the photon trajectory data */
            if (nlhs >= 4) {
                int outputidx = 3;
                fielddim[0] = MCX_DEBUG_REC_LEN;
                fielddim[1] = cfg.debugdatalen; // his.savedphoton is for one repetition, should correct
                fielddim[2] = 0;
                fielddim[3] = 0;
                mxSetFieldByNumber(plhs[outputidx], jstruct, 0, mxCreateNumericArray(2, fielddim, mxSINGLE_CLASS, mxREAL));

                if ((cfg.debuglevel & dlTraj) && cfg.exportdebugdata) {
                    memcpy((float*)mxGetPr(mxGetFieldByNumber(plhs[outputidx], jstruct, 0)), cfg.exportdebugdata, fielddim[0]*fielddim[1]*sizeof(float));
                }
            }

            if (cfg.exportdebugdata) {
                free(cfg.exportdebugdata);
                cfg.exportdebugdata = NULL;
            }

            if (nlhs >= 3) {
                fielddim[0] = (sizeof(RandType) * RAND_BUF_LEN);
                fielddim[1] = cfg.detectedcount;
                fielddim[2] = 0;
                fielddim[3] = 0;
                mxSetFieldByNumber(plhs[2], jstruct, 0, mxCreateNumericArray(2, fielddim, mxUINT8_CLASS, mxREAL));
                memcpy((unsigned char*)mxGetPr(mxGetFieldByNumber(plhs[2], jstruct, 0)), cfg.exportseed, fielddim[0]*fielddim[1]);
            }

            if (cfg.exportseed) {
                free(cfg.exportseed);
                cfg.exportseed = NULL;
            }

            /** if the 2nd output presents, output the detected photon partialpath data */
            if (nlhs >= 2) {
                if (cfg.issaveexit != 2) {
                    int hostdetreclen = (2 + ((cfg.ismomentum) > 0)) * mesh.prop + (cfg.issaveexit > 0) * 6 + 2;
                    fielddim[0] = hostdetreclen;
                    fielddim[1] = cfg.detectedcount;
                    fielddim[2] = 0;
                    fielddim[3] = 0;

                    if (cfg.detectedcount > 0) {
                        mxSetFieldByNumber(plhs[1], jstruct, 0, mxCreateNumericArray(2, fielddim, mxSINGLE_CLASS, mxREAL));
                        memcpy((float*)mxGetPr(mxGetFieldByNumber(plhs[1], jstruct, 0)), cfg.exportdetected,
                               fielddim[0]*fielddim[1]*sizeof(float));
                    }
                } else {
                    fielddim[0] = cfg.detparam1.w;
                    fielddim[1] = cfg.detparam2.w;
                    fielddim[2] = cfg.maxgate;
                    fielddim[3] = 0;
                    mxSetFieldByNumber(plhs[1], jstruct, 0, mxCreateNumericArray(3, fielddim, mxSINGLE_CLASS, mxREAL));
                    float* detmap = (float*)mxGetPr(mxGetFieldByNumber(plhs[1], jstruct, 0));
                    memset(detmap, cfg.detparam1.w * cfg.detparam2.w * cfg.maxgate, sizeof(float));
                    mesh_getdetimage(detmap, cfg.exportdetected, cfg.detectedcount, &cfg, &mesh);
                }
            }

            if (cfg.exportdetected) {
                free(cfg.exportdetected);
                cfg.exportdetected = NULL;
            }

            if (nlhs >= 1) {
                int datalen = (cfg.method == rtBLBadouelGrid) ? cfg.crop0.z : ( (cfg.basisorder) ? mesh.nn : mesh.ne);
                fielddim[0] = cfg.srcnum;
                fielddim[1] = datalen;
                fielddim[2] = cfg.maxgate;
                fielddim[3] = 0;
                fielddim[4] = 0;

                if (cfg.method == rtBLBadouelGrid) {
                    fielddim[0] = cfg.srcnum;
                    fielddim[1] = cfg.dim.x;
                    fielddim[2] = cfg.dim.y;
                    fielddim[3] = cfg.dim.z;
                    fielddim[4] = cfg.maxgate;

                    if (cfg.srcnum > 1) {
                        mxSetFieldByNumber(plhs[0], jstruct, 0, mxCreateNumericArray(5, fielddim, mxDOUBLE_CLASS, mxREAL));
                    } else {
                        mxSetFieldByNumber(plhs[0], jstruct, 0, mxCreateNumericArray(4, &fielddim[1], mxDOUBLE_CLASS, mxREAL));
                    }
                } else {
                    if (cfg.srcnum > 1) {
                        mxSetFieldByNumber(plhs[0], jstruct, 0, mxCreateNumericArray(3, fielddim, mxDOUBLE_CLASS, mxREAL));
                    } else {
                        mxSetFieldByNumber(plhs[0], jstruct, 0, mxCreateNumericArray(2, &fielddim[1], mxDOUBLE_CLASS, mxREAL));
                    }
                }

                double* output = (double*)mxGetPr(mxGetFieldByNumber(plhs[0], jstruct, 0));
                memcpy(output, mesh.weight, cfg.srcnum * datalen * cfg.maxgate * sizeof(double));

                if (cfg.issaveref) {      /** save diffuse reflectance */
                    fielddim[1] = mesh.nf;
                    fielddim[2] = cfg.maxgate;
                    mxSetFieldByNumber(plhs[0], jstruct, 1, mxCreateNumericArray(2, &fielddim[1], mxDOUBLE_CLASS, mxREAL));
                    memcpy((double*)mxGetPr(mxGetFieldByNumber(plhs[0], jstruct, 1)), mesh.dref, fielddim[1]*fielddim[2]*sizeof(double));
                }
            }

            if (errorflag) {
                mexErrMsgTxt("MMCLAB Terminated due to exception!");
            }
        } catch (const char* err) {
            mexPrintf("Error: %s\n", err);
        } catch (const std::exception& err) {
            mexPrintf("C++ Error: %s\n", err.what());
        } catch (...) {
            mexPrintf("Unknown Exception");
        }

        /** \subsection sclean End the simulation */
        mesh_clear(&mesh, &cfg);
        mcx_clearcfg(&cfg);
    }

    return;
}

/**
 * @brief Function to parse one subfield of the input structure
 *
 * This function reads in all necessary information from the cfg input structure.
 * it can handle single scalar inputs, short vectors (3-4 elem), strings and arrays.
 *
 * @param[in] root: the cfg input data structure
 * @param[in] item: the current element of the cfg input data structure
 * @param[in] idx: the index of the current element (starting from 0)
 * @param[out] cfg: the simulation configuration structure to store all input read from the parameters
 * @param[out] mesh: the mesh data structure
 */

void mmc_set_field(const mxArray* root, const mxArray* item, int idx, mcconfig* cfg, tetmesh* mesh) {
    const char* name = mxGetFieldNameByNumber(root, idx);
    const dimtype* arraydim;
    char* jsonshapes = NULL;
    int i, j;

    if (strcmp(name, "nphoton") == 0 && cfg->photonseed != NULL) {
        return;
    }

    cfg->flog = stderr;
    GET_1ST_FIELD(cfg, nphoton)
    GET_ONE_FIELD(cfg, nblocksize)
    GET_ONE_FIELD(cfg, nthread)
    GET_ONE_FIELD(cfg, tstart)
    GET_ONE_FIELD(cfg, tstep)
    GET_ONE_FIELD(cfg, tend)
    GET_ONE_FIELD(cfg, isreflect)
    GET_ONE_FIELD(cfg, isspecular)
    GET_ONE_FIELD(cfg, ismomentum)
    GET_ONE_FIELD(cfg, issaveexit)
    GET_ONE_FIELD(cfg, issaveseed)
    GET_ONE_FIELD(cfg, optlevel)
    GET_ONE_FIELD(cfg, isatomic)
    GET_ONE_FIELD(cfg, basisorder)
    GET_ONE_FIELD(cfg, outputformat)
    GET_ONE_FIELD(cfg, roulettesize)
    GET_ONE_FIELD(cfg, nout)
    GET_ONE_FIELD(cfg, isref3)
    GET_ONE_FIELD(cfg, isnormalized)
    GET_ONE_FIELD(cfg, issaveref)
    GET_ONE_FIELD(cfg, debugphoton)
    GET_ONE_FIELD(cfg, minenergy)
    GET_ONE_FIELD(cfg, replaydet)
    GET_ONE_FIELD(cfg, unitinmm)
    GET_ONE_FIELD(cfg, voidtime)
    GET_ONE_FIELD(cfg, mcmethod)
    GET_ONE_FIELD(cfg, maxdetphoton)
    GET_ONE_FIELD(cfg, maxjumpdebug)
    GET_VEC3_FIELD(cfg, srcpos)
    GET_VEC34_FIELD(cfg, srcdir)
    GET_VEC3_FIELD(cfg, steps)
    GET_VEC4_FIELD(cfg, srcparam1)
    GET_VEC4_FIELD(cfg, srcparam2)
    GET_VEC4_FIELD(cfg, detparam1)
    GET_VEC4_FIELD(cfg, detparam2)
    else if (strcmp(name, "e0") == 0) {
        double* val = mxGetPr(item);
        cfg->e0 = val[0];
        printf("mmc.e0=%d;\n", cfg->e0);
    } else if (strcmp(name, "node") == 0) {
        arraydim = mxGetDimensions(item);

        if (arraydim[0] <= 0 || arraydim[1] != 3) {
            MEXERROR("the 'node' field must have 3 columns (x,y,z)");
        }

        double* val = mxGetPr(item);
        mesh->nn = arraydim[0];

        if (mesh->node) {
            free(mesh->node);
        }

        mesh->node = (FLOAT3*)calloc(sizeof(FLOAT3), mesh->nn);

        for (j = 0; j < 3; j++)
            for (i = 0; i < mesh->nn; i++) {
                ((float*)(&mesh->node[i]))[j] = val[j * mesh->nn + i];
            }

        printf("mmc.nn=%d;\n", mesh->nn);
    } else if (strcmp(name, "elem") == 0) {
        arraydim = mxGetDimensions(item);

        if (arraydim[0] <= 0 || arraydim[1] < 4) {
            MEXERROR("the 'elem' field must have 4 columns (e1,e2,e3,e4)");
        }

        double* val = mxGetPr(item);
        mesh->ne = arraydim[0];
        mesh->elemlen = arraydim[1];

        if (mesh->elem) {
            free(mesh->elem);
        }

        mesh->elem = (int*)calloc(sizeof(int) * arraydim[1], mesh->ne);

        for (j = 0; j < mesh->elemlen; j++)
            for (i = 0; i < mesh->ne; i++) {
                mesh->elem[i * mesh->elemlen + j] = val[j * mesh->ne + i];
            }

        printf("mmc.elem=[%d,%d];\n", mesh->ne, mesh->elemlen);
    } else if (strcmp(name, "noderoi") == 0) {
        arraydim = mxGetDimensions(item);

        if (MAX(arraydim[0], arraydim[1]) == 0) {
            MEXERROR("the 'noderoi' field can not be empty");
        }

        double* val = mxGetPr(item);
        mesh->nn = MAX(arraydim[0], arraydim[1]);

        if (mesh->noderoi) {
            free(mesh->noderoi);
        }

        mesh->noderoi = (float*)malloc(sizeof(float) * mesh->nn);

        for (i = 0; i < mesh->nn; i++) {
            mesh->noderoi[i] = val[i];
        }

        cfg->implicit = 1;
        printf("mmc.noderoi=%d;\n", mesh->nn);
    } else if (strcmp(name, "edgeroi") == 0) {
        arraydim = mxGetDimensions(item);

        if (arraydim[0] <= 0 || arraydim[1] != 6) {
            MEXERROR("the 'edgeroi' field must have 6 columns (e1,e2)");
        }

        double* val = mxGetPr(item);
        mesh->ne = arraydim[0];

        if (mesh->edgeroi) {
            free(mesh->edgeroi);
        }

        mesh->edgeroi = (float*)calloc(sizeof(float) * arraydim[1], mesh->ne);

        for (j = 0; j < 6; j++)
            for (i = 0; i < mesh->ne; i++) {
                mesh->edgeroi[i * 6 + j] = val[j * mesh->ne + i];
            }

        cfg->implicit = 1;
        printf("mmc.edgeroi=[%d,%d];\n", mesh->ne, 6);
    } else if (strcmp(name, "faceroi") == 0) {
        arraydim = mxGetDimensions(item);

        if (arraydim[0] <= 0 || arraydim[1] != 4) {
            MEXERROR("the 'faceroi' field must have 4 columns (f1,f2,f3,f4)");
        }

        double* val = mxGetPr(item);
        mesh->ne = arraydim[0];

        if (mesh->faceroi) {
            free(mesh->faceroi);
        }

        mesh->faceroi = (float*)calloc(sizeof(float) * arraydim[1], mesh->ne);

        for (j = 0; j < 4; j++)
            for (i = 0; i < mesh->ne; i++) {
                mesh->faceroi[i * 4 + j] = val[j * mesh->ne + i];
            }

        cfg->implicit = 2;
        printf("mmc.faceroi=[%d,%d];\n", mesh->ne, 4);
    } else if (strcmp(name, "elemprop") == 0) {
        arraydim = mxGetDimensions(item);

        if (MAX(arraydim[0], arraydim[1]) == 0) {
            MEXERROR("the 'elemprop' field can not be empty");
        }

        double* val = mxGetPr(item);
        mesh->ne = MAX(arraydim[0], arraydim[1]);

        if (mesh->type) {
            free(mesh->type);
        }

        mesh->type = (int*)malloc(sizeof(int ) * mesh->ne);

        for (i = 0; i < mesh->ne; i++) {
            mesh->type[i] = val[i];
        }

        printf("mmc.ne=%d;\n", mesh->ne);
    } else if (strcmp(name, "facenb") == 0) {
        arraydim = mxGetDimensions(item);

        if (arraydim[0] <= 0 || arraydim[1] < 4) {
            MEXERROR("the 'elem' field must have 4 columns (e1,e2,e3,e4)");
        }

        double* val = mxGetPr(item);
        mesh->ne = arraydim[0];
        mesh->elemlen = arraydim[1];

        if (mesh->facenb) {
            free(mesh->facenb);
        }

        mesh->facenb = (int*)malloc(sizeof(int) * arraydim[1] * mesh->ne);

        for (dimtype j = 0; j < arraydim[1]; j++)
            for (i = 0; i < mesh->ne; i++) {
                mesh->facenb[i * arraydim[1] + j] = val[j * mesh->ne + i];
            }

        printf("mmc.facenb=[%d,%d];\n", mesh->ne, mesh->elemlen);
    } else if (strcmp(name, "evol") == 0) {
        arraydim = mxGetDimensions(item);

        if (MAX(arraydim[0], arraydim[1]) == 0) {
            MEXERROR("the 'evol' field can not be empty");
        }

        double* val = mxGetPr(item);
        mesh->ne = MAX(arraydim[0], arraydim[1]);

        if (mesh->evol) {
            free(mesh->evol);
        }

        mesh->evol = (float*)malloc(sizeof(float) * mesh->ne);

        for (i = 0; i < mesh->ne; i++) {
            mesh->evol[i] = val[i];
        }

        printf("mmc.evol=%d;\n", mesh->ne);
    } else if (strcmp(name, "detpos") == 0) {
        arraydim = mxGetDimensions(item);

        if (arraydim[0] > 0 && arraydim[1] != 4) {
            MEXERROR("the 'detpos' field must have 4 columns (x,y,z,radius)");
        }

        double* val = mxGetPr(item);
        cfg->detnum = arraydim[0];

        if (cfg->detpos) {
            free(cfg->detpos);
        }

        cfg->detpos = (float4*)malloc(cfg->detnum * sizeof(float4));

        for (j = 0; j < 4; j++)
            for (i = 0; i < cfg->detnum; i++) {
                ((float*)(&cfg->detpos[i]))[j] = val[j * cfg->detnum + i];
            }

        printf("mmc.detnum=%d;\n", cfg->detnum);
    } else if (strcmp(name, "prop") == 0) {
        arraydim = mxGetDimensions(item);

        if (arraydim[0] > 0 && arraydim[1] != 4) {
            MEXERROR("the 'prop' field must have 4 columns (mua,mus,g,n)");
        }

        double* val = mxGetPr(item);
        mesh->prop = arraydim[0] - 1;

        if (mesh->med) {
            free(mesh->med);
        }

        mesh->med = (medium*)calloc(sizeof(medium), mesh->prop + 1);

        for (j = 0; j < 4; j++)
            for (i = 0; i <= mesh->prop; i++) {
                ((float*)(&mesh->med[i]))[j] = val[j * (mesh->prop + 1) + i];
            }

        cfg->his.maxmedia = mesh->prop;
        printf("mmc.prop=%d;\n", mesh->prop);
    } else if (strcmp(name, "debuglevel") == 0) {
        int len = mxGetNumberOfElements(item);
        char buf[MAX_SESSION_LENGTH];

        if (!mxIsChar(item) || len == 0) {
            MEXERROR("the 'debuglevel' field must be a non-empty string");
        }

        if (len > MAX_SESSION_LENGTH) {
            MEXERROR("the 'debuglevel' field is too long");
        }

        int status = mxGetString(item, buf, MAX_SESSION_LENGTH);

        if (status != 0) {
            mexWarnMsgTxt("not enough space. string is truncated.");
        }

        cfg->debuglevel = mcx_parsedebugopt(buf, debugflag);
        printf("mmc.debuglevel='%s';\n", buf);
    } else if (strcmp(name, "srctype") == 0) {
        int len = mxGetNumberOfElements(item);
        const char* srctypeid[] = {"pencil", "isotropic", "cone", "gaussian", "planar", "pattern", "fourier", "arcsine", "disk", "fourierx", "fourierx2d", "zgaussian", "line", "slit", ""};
        char strtypestr[MAX_SESSION_LENGTH] = {'\0'};

        if (!mxIsChar(item) || len == 0) {
            mexErrMsgTxt("the 'srctype' field must be a non-empty string");
        }

        if (len > MAX_SESSION_LENGTH) {
            mexErrMsgTxt("the 'srctype' field is too long");
        }

        int status = mxGetString(item, strtypestr, MAX_SESSION_LENGTH);

        if (status != 0) {
            mexWarnMsgTxt("not enough space. string is truncated.");
        }

        cfg->srctype = mcx_keylookup(strtypestr, srctypeid);

        if (cfg->srctype == -1) {
            mexErrMsgTxt("the specified source type is not supported");
        }

        printf("mmc.srctype='%s';\n", strtypestr);
    } else if (strcmp(name, "session") == 0) {
        int len = mxGetNumberOfElements(item);

        if (!mxIsChar(item) || len == 0) {
            MEXERROR("the 'session' field must be a non-empty string");
        }

        if (len > MAX_SESSION_LENGTH) {
            MEXERROR("the 'session' field is too long");
        }

        int status = mxGetString(item, cfg->session, MAX_SESSION_LENGTH);

        if (status != 0) {
            mexWarnMsgTxt("not enough space. string is truncated.");
        }

        printf("mmc.session='%s';\n", cfg->session);
    } else if (strcmp(name, "srcpattern") == 0) {
        arraydim = mxGetDimensions(item);
        dimtype dimz = 1, k;

        if (mxGetNumberOfDimensions(item) == 3) {
            dimz = arraydim[2];
            cfg->srcnum = arraydim[0];
        }

        double* val = mxGetPr(item);

        if (cfg->srcpattern) {
            free(cfg->srcpattern);
        }

        cfg->srcpattern = (float*)malloc(arraydim[0] * arraydim[1] * dimz * sizeof(float));

        for (k = 0; k < arraydim[0]*arraydim[1]*dimz; k++) {
            cfg->srcpattern[k] = val[k];
        }

        printf("mmc.srcpattern=[%ld %ld %ld];\n", arraydim[0], arraydim[1], dimz);
    } else if (strcmp(name, "method") == 0) {
        int len = mxGetNumberOfElements(item);
        const char* methods[] = {"plucker", "havel", "badouel", "elem", "grid", ""};
        char methodstr[MAX_SESSION_LENGTH] = {'\0'};

        if (!mxIsChar(item) || len == 0) {
            mexErrMsgTxt("the 'method' field must be a non-empty string");
        }

        if (len > MAX_SESSION_LENGTH) {
            mexErrMsgTxt("the 'method' field is too long");
        }

        int status = mxGetString(item, methodstr, MAX_SESSION_LENGTH);

        if (status != 0) {
            mexWarnMsgTxt("not enough space. string is truncated.");
        }

        cfg->method = mcx_keylookup(methodstr, methods);

        if (cfg->method == -1) {
            mexErrMsgTxt("the specified method is not supported");
        }

        printf("mmc.method='%s';\n", methodstr);
    } else if (strcmp(name, "outputtype") == 0) {
        int len = mxGetNumberOfElements(item);
        const char* outputtype[] = {"flux", "fluence", "energy", "jacobian", "wl", "wp", ""};
        char outputstr[MAX_SESSION_LENGTH] = {'\0'};

        if (!mxIsChar(item) || len == 0) {
            mexErrMsgTxt("the 'outputtype' field must be a non-empty string");
        }

        if (len > MAX_SESSION_LENGTH) {
            mexErrMsgTxt("the 'outputtype' field is too long");
        }

        int status = mxGetString(item, outputstr, MAX_SESSION_LENGTH);

        if (status != 0) {
            mexWarnMsgTxt("not enough space. string is truncated.");
        }

        cfg->outputtype = mcx_keylookup(outputstr, outputtype);

        if (cfg->outputtype == -1) {
            mexErrMsgTxt("the specified output type is not supported");
        }

        printf("mmc.outputtype='%s';\n", outputstr);
    } else if (strcmp(name, "compute") == 0) {
        int len = mxGetNumberOfElements(item);
        const char* computebackend[] = {"sse", "opencl", "cuda", ""};
        char computestr[MAX_SESSION_LENGTH] = {'\0'};

        if (!mxIsChar(item) || len == 0) {
            mexErrMsgTxt("the 'compute' field must be a non-empty string");
        }

        if (len > MAX_SESSION_LENGTH) {
            mexErrMsgTxt("the 'compute' field is too long");
        }

        int status = mxGetString(item, computestr, MAX_SESSION_LENGTH);

        if (status != 0) {
            mexWarnMsgTxt("not enough space. string is truncated.");
        }

        cfg->compute = mcx_keylookup(computestr, computebackend);

        if (cfg->compute == -1) {
            mexErrMsgTxt("the specified compute is not supported");
        }

        printf("mmc.compute='%s';\n", computestr);
    } else if (strcmp(name, "shapes") == 0) {
        int len = mxGetNumberOfElements(item);

        if (!mxIsChar(item) || len == 0) {
            MEXERROR("the 'shapes' field must be a non-empty string");
        }

        jsonshapes = new char[len + 1];
        mxGetString(item, jsonshapes, len + 1);
        jsonshapes[len] = '\0';
    } else if (strcmp(name, "detphotons") == 0) {
        arraydim = mxGetDimensions(item);
        dimdetps[0] = arraydim[0];
        dimdetps[1] = arraydim[1];
        detps = (float*)malloc(arraydim[0] * arraydim[1] * sizeof(float));
        memcpy(detps, mxGetData(item), arraydim[0]*arraydim[1]*sizeof(float));
        printf("mmc.detphotons=[%ld %ld];\n", arraydim[0], arraydim[1]);
    } else if (strcmp(name, "seed") == 0) {
        arraydim = mxGetDimensions(item);

        if (MAX(arraydim[0], arraydim[1]) == 0) {
            MEXERROR("the 'seed' field can not be empty");
        }

        if (!mxIsUint8(item)) {
            double* val = mxGetPr(item);

            cfg->seed = val[0];
            printf("mmc.seed=%d;\n", cfg->seed);
        } else {
            seedbyte = arraydim[0];
            cfg->photonseed = malloc(arraydim[0] * arraydim[1]);

            if (arraydim[0] != (sizeof(RandType)*RAND_BUF_LEN)) {
                MEXERROR("the row number of cfg.seed does not match RNG seed byte-length");
            }

            memcpy(cfg->photonseed, mxGetData(item), arraydim[0]*arraydim[1]);
            cfg->seed = SEED_FROM_FILE;
            cfg->nphoton = arraydim[1];
            printf("mmc.nphoton=%zu;\n", cfg->nphoton);
        }
    } else if (strcmp(name, "replayweight") == 0) {
        arraydim = mxGetDimensions(item);

        if (MAX(arraydim[0], arraydim[1]) == 0) {
            MEXERROR("the 'replayweight' field can not be empty");
        }

        cfg->his.detected = arraydim[0] * arraydim[1];
        cfg->replayweight = (float*)malloc(cfg->his.detected * sizeof(float));
        memcpy(cfg->replayweight, mxGetData(item), cfg->his.detected * sizeof(float));
        printf("mmc.replayweight=%d;\n", cfg->his.detected);
    } else if (strcmp(name, "replaytime") == 0) {
        arraydim = mxGetDimensions(item);

        if (MAX(arraydim[0], arraydim[1]) == 0) {
            MEXERROR("the 'replaytime' field can not be empty");
        }

        cfg->his.detected = arraydim[0] * arraydim[1];
        cfg->replaytime = (float*)malloc(cfg->his.detected * sizeof(float));
        memcpy(cfg->replaytime, mxGetData(item), cfg->his.detected * sizeof(float));
        printf("mmc.replaytime=%d;\n", cfg->his.detected);
    } else if (strcmp(name, "gpuid") == 0) {
        int len = mxGetNumberOfElements(item);

        if (mxIsChar(item)) {
            if (len == 0) {
                mexErrMsgTxt("the 'gpuid' field must be an integer or non-empty string");
            }

            if (len > MAX_DEVICE) {
                mexErrMsgTxt("the 'gpuid' field is too long");
            }

            int status = mxGetString(item, cfg->deviceid, MAX_DEVICE);

            if (status != 0) {
                mexWarnMsgTxt("not enough space. string is truncated.");
            }

            printf("mmc.gpuid='%s';\n", cfg->deviceid);
        } else {
            double* val = mxGetPr(item);
            cfg->gpuid = val[0];
            memset(cfg->deviceid, 0, MAX_DEVICE);

            if (cfg->gpuid > 0 && cfg->gpuid < MAX_DEVICE) {
                memset(cfg->deviceid, '0', cfg->gpuid - 1);
                cfg->deviceid[cfg->gpuid - 1] = '1';
            }

            printf("mmc.gpuid=%d;\n", cfg->gpuid);
        }

        for (int i = 0; i < MAX_DEVICE; i++)
            if (cfg->deviceid[i] == '0') {
                cfg->deviceid[i] = '\0';
            }
    } else if (strcmp(name, "workload") == 0) {
        double* val = mxGetPr(item);
        arraydim = mxGetDimensions(item);

        if (arraydim[0]*arraydim[1] > MAX_DEVICE) {
            mexErrMsgTxt("the workload list can not be longer than 256");
        }

        for (dimtype i = 0; i < arraydim[0]*arraydim[1]; i++) {
            cfg->workload[i] = val[i];
        }

        printf("mmc.workload=<<%.0f>>;\n", (double)arraydim[0]*arraydim[1]);
    } else if (strcmp(name, "isreoriented") == 0) {
        /*internal flag, don't need to do anything*/
    } else {
        printf("WARNING: redundant field '%s'\n", name);
    }

    if (jsonshapes) {
        delete [] jsonshapes;
    }
}


/**
 * @brief Error reporting function in the mex function, equivallent to mcx_error in binary mode
 *
 * @param[in] id: a single integer for the types of the error
 * @param[in] msg: the error message string
 * @param[in] filename: the unit file name where this error is raised
 * @param[in] linenum: the line number in the file where this error is raised
 */

extern "C" int mmc_throw_exception(const int id, const char* msg, const char* filename, const int linenum) {
    printf("MMCLAB ERROR (%d): %s in unit %s:%d\n", id, msg, filename, linenum);
    throw (msg);
    return id;
}

/**
 * @brief Print a brief help information if nothing is provided
 */

void mmclab_usage() {
    printf("MMCLAB " MMC_VERSION "\nUsage:\n    [flux,detphoton]=mmclab(cfg);\n\nPlease run 'help mmclab' for more details.\n");
}

/**
 * @brief Force matlab refresh the command window to print all buffered messages
 */

extern "C" void mcx_matlab_flush() {
#if defined(MATLAB_MEX_FILE)
    mexEvalString("pause(.0001);");
#else
    mexEvalString("fflush(stdout);");
#endif
}

#if defined(__APPLE__)
/**
 * @brief Phantom main function to let macos to build mmclab
 */

int main(void) {
    return 1;
}

#endif