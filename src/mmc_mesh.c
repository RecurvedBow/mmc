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
\file    mmc_mesh.c

\brief   Basic vector math and mesh operations
*******************************************************************************/

#include <stdlib.h>
#include "mmc_const.h"
#include "mmc_mesh.h"
#include <string.h>
#include "mmc_highorder.h"

#ifdef WIN32
    char pathsep = '\\'; /**< path separator on Windows */
#else
    char pathsep = '/'; /**< path separator on Linux/Unix/OSX */
#endif

/**
 * \brief Tetrahedron faces, in counter-clock-wise orders, represented using local node indices
 *
 * node-connectivity, i.e. nc[4] points to the 4 facets of a tetrahedron, with each
 * triangular face made of 3 nodes. The numbers [0-4] are the
 * local node indices (starting from 0). The order of the nodes
 * are in counter-clock-wise orders.
 */

const int out[4][3] = {{0, 3, 1}, {3, 2, 1}, {0, 2, 3}, {0, 1, 2}};

/**
 * \brief The local index of the node with an opposite face to the i-th face defined in nc[][]
 *
 * nc[i] <-> node[facemap[i]]
 * the 1st face of this tet, i.e. nc[0]={3,0,1}, is opposite to the 3rd node
 * the 2nd face of this tet, i.e. nc[1]={3,1,2}, is opposite to the 1st node
 * etc.
 */

const int facemap[] = {2, 0, 1, 3};

/**
 * \brief Inverse mapping between the local index of the node and the corresponding opposite face in nc[]'s order
 *
 * nc[ifacemap[i]] <-> node[i]
 * the 1st node of this tet is in opposite to the 2nd face, i.e. nc[1]={3,1,2}
 * the 2nd node of this tet is in opposite to the 3rd face, i.e. nc[1]={2,0,3}
 * etc.
 */

const int ifacemap[] = {1, 2, 0, 3};

/**
 * \brief Index mapping from the i-th face-neighbors (facenb) to the face defined in nc[][]
 *
 * facenb[i] <-> nc[faceorder[i]]
 * the 1st tet neighbor shares the 2nd face of this tet, i.e. nc[1]={3,1,2}
 * the 2nd tet neighbor shares the 4th face of this tet, i.e. nc[3]={1,0,2}
 * etc.
 */

const int faceorder[] = {1, 3, 2, 0, -1};

/**
 * \brief Index mapping from the i-th face defined in nc[][] to the face-neighbor (facenb) face orders
 *
 * nc[ifaceorder[i]] <-> facenb[i]
 * nc[0], made of nodes {3,0,1}, is the face connecting to the 4th neighbor (facenb[3]),
 * nc[1], made of nodes {3,1,2}, is the face connecting to the 1st neighbor (facenb[0]),
 * etc.
 */

const int ifaceorder[] = {3, 0, 2, 1};

/**
 * @brief Initializing the mesh data structure with default values
 *
 * Constructor of the mesh object, initializing all field to default values
 */


void mesh_init(tetmesh* mesh) {
    mesh->nn = 0;
    mesh->ne = 0;
    mesh->nf = 0;
    mesh->prop = 0;
    mesh->elemlen = 4;
    mesh->node = NULL;
    mesh->elem = NULL;
    mesh->elem2 = NULL;
    mesh->edgeroi = NULL;
    mesh->faceroi = NULL;
    mesh->noderoi = NULL;
    mesh->srcelemlen = 0;
    mesh->srcelem = NULL;
    mesh->detelemlen = 0;
    mesh->detelem = NULL;
    mesh->facenb = NULL;
    mesh->type = NULL;
    mesh->med = NULL;
    mesh->weight = NULL;
    mesh->evol = NULL;
    mesh->nvol = NULL;
    mesh->dref = NULL;
    mesh->nmin.x = VERY_BIG;
    mesh->nmin.y = VERY_BIG;
    mesh->nmin.z = VERY_BIG;
    mesh->nmin.w = 1.f;
    mesh->nmax.x = -VERY_BIG;
    mesh->nmax.y = -VERY_BIG;
    mesh->nmax.z = -VERY_BIG;
    mesh->nmax.w = 1.f;
}


/**
 * @brief Clearing the mesh data structure
 *
 * Destructor of the mesh data structure, delete all dynamically allocated members
 */


void mesh_clear(tetmesh* mesh, mcconfig* cfg) {
    mesh->nn = 0;
    mesh->ne = 0;
    mesh->nf = 0;
    mesh->srcelemlen = 0;
    mesh->detelemlen = 0;

    if (mesh->node && cfg->node == NULL) {
        free(mesh->node);
        mesh->node = NULL;
    }

    if (mesh->elem) {
        free(mesh->elem);
        mesh->elem = NULL;
    }

    if (mesh->elem2) {
        free(mesh->elem2);
        mesh->elem2 = NULL;
    }

    if (mesh->facenb) {
        free(mesh->facenb);
        mesh->facenb = NULL;
    }

    if (mesh->dref) {
        free(mesh->dref);
        mesh->dref = NULL;
    }

    if (mesh->type) {
        free(mesh->type);
        mesh->type = NULL;
    }

    if (mesh->med) {
        free(mesh->med);
        mesh->med = NULL;
    }

    if (mesh->weight) {
        free(mesh->weight);
        mesh->weight = NULL;
    }

    if (mesh->evol) {
        free(mesh->evol);
        mesh->evol = NULL;
    }

    if (mesh->nvol) {
        free(mesh->nvol);
        mesh->nvol = NULL;
    }

    if (mesh->srcelem) {
        free(mesh->srcelem);
        mesh->srcelem = NULL;
    }

    if (mesh->detelem) {
        free(mesh->detelem);
        mesh->detelem = NULL;
    }

    if (mesh->noderoi) {
        free(mesh->noderoi);
        mesh->noderoi = NULL;
    }

    if (mesh->edgeroi) {
        free(mesh->edgeroi);
        mesh->edgeroi = NULL;
    }

    if (mesh->faceroi) {
        free(mesh->faceroi);
        mesh->faceroi = NULL;
    }
}


#ifndef MCX_CONTAINER

/**
 * @brief Loading user-specified mesh data
 *
 * Loading node, element etc from files into memory
 */

void mesh_init_from_cfg(tetmesh* mesh, mcconfig* cfg) {
    mesh_init(mesh);
    mesh_loadnode(mesh, cfg);
    mesh_loadelem(mesh, cfg);
    mesh_loadmedia(mesh, cfg);

    if (cfg->isdumpjson == 1) {
        int i, j, *elem = NULL;

        if (cfg->medianum == 0) {
            cfg->medianum = mesh->prop + 1;
            cfg->prop = mesh->med;
        }

        if (cfg->nodenum == 0 && cfg->elemnum == 0) {
            cfg->nodenum = mesh->nn;
            cfg->elemnum = mesh->ne;
            cfg->elemlen = mesh->elemlen;
            cfg->node = mesh->node;
            elem = (int*)malloc(mesh->ne * (mesh->elemlen + 1) * sizeof(int));

            for (i = 0; i < mesh->ne; i++) {
                for (j = 0; j < mesh->elemlen; j++) {
                    elem[i * (mesh->elemlen + 1) + j] = mesh->elem[i * mesh->elemlen + j];
                }

                elem[i * (mesh->elemlen + 1) + mesh->elemlen] = mesh->type[i];
            }

            cfg->elem = elem;
        }

        mcx_savejdata(cfg->jsonfile, cfg);

        if (elem) {
            free(elem);
        }

        exit(0);
    }

    if (cfg->basisorder == 2) {
        mesh_10nodetet(mesh, cfg);
    }

    mesh_loadelemvol(mesh, cfg);
    mesh_loadfaceneighbor(mesh, cfg);
    mesh_loadroi(mesh, cfg);

    if (cfg->seed == SEED_FROM_FILE && cfg->seedfile[0]) {
        mesh_loadseedfile(mesh, cfg);
    }
}

#endif

/**
 * @brief Error-handling in mesh operations
 *
 * @param[in] msg: the error message string
 * @param[in] file: the unit file name where this error is raised
 * @param[in] linenum: the line number in the file where this error is raised
 */

void mesh_error(const char* msg, const char* file, const int linenum) {
#ifdef MCX_CONTAINER
    mmc_throw_exception(1, msg, file, linenum);
#else
    fprintf(stderr, "Mesh error: %s in unit %s line#%d\n", msg, file, linenum);
    exit(1);
#endif
}

/**
 * @brief Construct a full mesh file name using cfg session and root path
 *
 * @param[in] format: a format string to form the file name
 * @param[in] foutput: pointer to the output string buffer
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_filenames(const char* format, char* foutput, mcconfig* cfg) {
    char filename[MAX_PATH_LENGTH];
    sprintf(filename, format, cfg->meshtag);

    if (cfg->rootpath[0]) {
        sprintf(foutput, "%s%c%s", cfg->rootpath, pathsep, filename);
    } else {
        sprintf(foutput, "%s", filename);
    }
}


void mesh_createdualmesh(tetmesh* mesh, mcconfig* cfg) {
    int i;
    mesh->nmin.x = VERY_BIG;
    mesh->nmin.y = VERY_BIG;
    mesh->nmin.z = VERY_BIG;
    mesh->nmax.x = -VERY_BIG;
    mesh->nmax.y = -VERY_BIG;
    mesh->nmax.z = -VERY_BIG;

    for (i = 0; i < mesh->nn; i++) {
        mesh->nmin.x = MIN(mesh->node[i].x, mesh->nmin.x);
        mesh->nmin.y = MIN(mesh->node[i].y, mesh->nmin.y);
        mesh->nmin.z = MIN(mesh->node[i].z, mesh->nmin.z);
        mesh->nmax.x = MAX(mesh->node[i].x, mesh->nmax.x);
        mesh->nmax.y = MAX(mesh->node[i].y, mesh->nmax.y);
        mesh->nmax.z = MAX(mesh->node[i].z, mesh->nmax.z);
    }

    mesh->nmin.x -= EPS;
    mesh->nmin.y -= EPS;
    mesh->nmin.z -= EPS;
    mesh->nmax.x += EPS;
    mesh->nmax.y += EPS;
    mesh->nmax.z += EPS;

    cfg->dim.x = (int)((mesh->nmax.x - mesh->nmin.x) / cfg->steps.x) + 1;
    cfg->dim.y = (int)((mesh->nmax.y - mesh->nmin.y) / cfg->steps.y) + 1;
    cfg->dim.z = (int)((mesh->nmax.z - mesh->nmin.z) / cfg->steps.z) + 1;

    cfg->crop0.x = cfg->dim.x;
    cfg->crop0.y = cfg->dim.y * cfg->dim.x;
    cfg->crop0.z = cfg->dim.y * cfg->dim.x * cfg->dim.z;
}

/**
 * @brief Identify wide-field source and detector-related elements (type=-1 for source, type=-2 for det)
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_srcdetelem(tetmesh* mesh, mcconfig* cfg) {
    int i;

    mesh->srcelemlen = 0;
    mesh->detelemlen = 0;

    for (i = 0; i < mesh->ne; i++) {
        if (mesh->type[i] == -1) { /*number of elements in the initial candidate list*/
            mesh->srcelemlen++;
            cfg->e0 = (cfg->e0 == 0) ? i + 1 : cfg->e0;
        }

        if (mesh->type[i] == -2) { /*number of elements in the initial candidate list*/
            mesh->detelemlen++;
            cfg->isextdet = 1;
            cfg->detnum = 0; // when detecting wide-field detectors, suppress point detectors
        }
    }

    /*Record the index of inital elements to initiate source search*/
    /*Then change the type of initial elements back to 0 to continue propogation*/
    if (mesh->srcelemlen > 0 ||  mesh->detelemlen > 0) {
        int is = 0, id = 0;
        mesh->srcelem = (int*)calloc(mesh->srcelemlen, sizeof(int));
        mesh->detelem = (int*)calloc(mesh->detelemlen, sizeof(int));

        for (i = 0; i < mesh->ne; i++) {
            if (mesh->type[i] < 0) {
                if (mesh->type[i] == -1) {
                    mesh->srcelem[is++] = i + 1;
                    mesh->type[i] = 0;
                } else if (mesh->type[i] == -2) { /*keep -2, will be replaced to medianum+1 in loadmedia*/
                    mesh->detelem[id++] = i + 1;
                }
            }
        }
    }
}


#ifndef MCX_CONTAINER

/**
 * @brief Load node file and initialize the related mesh properties
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_loadnode(tetmesh* mesh, mcconfig* cfg) {
    FILE* fp;
    int tmp, len, i;
    char fnode[MAX_FULL_PATH];

    if (cfg->node && cfg->nodenum > 0) {
        mesh->node = cfg->node;
        mesh->nn = cfg->nodenum;

        if (cfg->method == rtBLBadouelGrid) {
            mesh_createdualmesh(mesh, cfg);
        }

        return;
    }

    mesh_filenames("node_%s.dat", fnode, cfg);

    if ((fp = fopen(fnode, "rt")) == NULL) {
        MESH_ERROR("can not open node file");
    }

    len = fscanf(fp, "%d %d", &tmp, &(mesh->nn));

    if (len != 2 || mesh->nn <= 0) {
        MESH_ERROR("node file has wrong format");
    }

    mesh->node = (FLOAT3*)calloc(sizeof(FLOAT3), mesh->nn);

    for (i = 0; i < mesh->nn; i++) {
        if (fscanf(fp, "%d %f %f %f", &tmp, &(mesh->node[i].x), &(mesh->node[i].y), &(mesh->node[i].z)) != 4) {
            MESH_ERROR("node file has wrong format");
        }
    }

    fclose(fp);

    if (cfg->method == rtBLBadouelGrid) {
        mesh_createdualmesh(mesh, cfg);
    }
}

/**
 * @brief Load optical property file and initialize the related mesh properties
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_loadmedia(tetmesh* mesh, mcconfig* cfg) {
    FILE* fp = NULL;
    int tmp, len, i;
    char fmed[MAX_FULL_PATH];

    if (cfg->medianum == 0) {
        mesh_filenames("prop_%s.dat", fmed, cfg);

        if ((fp = fopen(fmed, "rt")) == NULL) {
            MESH_ERROR("can not open media property file");
        }

        len = fscanf(fp, "%d %d", &tmp, &(mesh->prop));

        if (len != 2 || mesh->prop <= 0) {
            MESH_ERROR("property file has wrong format");
        }
    } else {
        mesh->prop = cfg->medianum - 1;
    }

    /*when there is an external detector, reindex the property to medianum+1*/
    mesh->med = (medium*)calloc(sizeof(medium), mesh->prop + 1 + cfg->isextdet);

    mesh->med[0].mua = 0.f;
    mesh->med[0].mus = 0.f;
    mesh->med[0].n = cfg->nout;
    mesh->med[0].g = 1.f;

    /*make medianum+1 the same as medium 0*/
    if (cfg->isextdet) {
        memcpy(mesh->med + mesh->prop + 1, mesh->med, sizeof(medium));

        for (i = 0; i < mesh->ne; i++) {
            if (mesh->type[i] == -2) {
                mesh->type[i] = mesh->prop + 1;
            }
        }
    }

    if (cfg->medianum == 0) {
        for (i = 1; i <= mesh->prop; i++) {
            if (fscanf(fp, "%d %f %f %f %f", &tmp, &(mesh->med[i].mua), &(mesh->med[i].mus),
                       &(mesh->med[i].g), &(mesh->med[i].n)) != 5) {
                MESH_ERROR("property file has wrong format");
            }
        }

        fclose(fp);
    } else {
        memcpy(mesh->med, cfg->prop, sizeof(medium) * cfg->medianum);
    }

    if (cfg->method != rtBLBadouelGrid && cfg->unitinmm != 1.f) {
        for (i = 1; i <= mesh->prop; i++) {
            mesh->med[i].mus *= cfg->unitinmm;
            mesh->med[i].mua *= cfg->unitinmm;
        }
    }

    cfg->his.maxmedia = mesh->prop; /*skip media 0*/
}

/**
 * @brief Load edge/node/face roi for implicit MMC
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_loadroi(tetmesh* mesh, mcconfig* cfg) {
    FILE* fp;
    int len, i, j, row, col;
    float* pe = NULL;
    char froi[MAX_FULL_PATH];

    if (cfg->roidata && cfg->roitype != rtNone) {
        if (cfg->roitype == rtEdge) {
            row = mesh->ne;
            col = 6;
            mesh->edgeroi = (float*)malloc(sizeof(float) * row * col);
            pe = mesh->edgeroi;
            cfg->implicit = 1;
        } else if (cfg->roitype == rtNode) {
            row = mesh->nn;
            col = 1;
            mesh->noderoi = (float*)malloc(sizeof(float) * row * col);
            pe = mesh->noderoi;
            cfg->implicit = 1;
        } else {
            row = mesh->ne;
            col = 4;
            mesh->faceroi = (float*)malloc(sizeof(float) * row * col);
            pe = mesh->faceroi;
            cfg->implicit = 2;
        }

        memcpy(pe, cfg->roidata, sizeof(float) * row * col);

        return;
    }

    mesh_filenames("roi_%s.dat", froi, cfg);

    if ((fp = fopen(froi, "rt")) == NULL) {
        return;
    }

    len = fscanf(fp, "%d %d", &col, &row);

    if (len != 2 || (col != 1 && col != 4 && col != 6) || row <= 0) {
        MESH_ERROR("roi file has wrong format");
    }

    if (col == 6) {
        mesh->edgeroi = (float*)malloc(sizeof(float) * 6 * mesh->ne);
        pe = mesh->edgeroi;
        cfg->implicit = 1;
    } else if (col == 1) {
        mesh->noderoi = (float*)malloc(sizeof(float) * mesh->nn);
        pe = mesh->noderoi;
        cfg->implicit = 1;
    } else if (col == 4) {
        mesh->faceroi = (float*)malloc(sizeof(float) * 4 * mesh->ne);
        pe = mesh->faceroi;
        cfg->implicit = 2;
    }

    for (i = 0; i < row; i++) {
        for (j = 0; j < col; j++)
            if (fscanf(fp, "%f", pe + j) != 1) {
                break;
            }

        pe += col;
    }

    fclose(fp);

    if (i < row) {
        MESH_ERROR("roi file has wrong format");
    }
}

/**
 * @brief Load element file and initialize the related mesh properties
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_loadelem(tetmesh* mesh, mcconfig* cfg) {
    FILE* fp;
    int tmp, len, i, j, datalen;
    int* pe;
    char felem[MAX_FULL_PATH];

    if (cfg->node && cfg->nodenum > 0) {
        mesh->ne = cfg->elemnum;
        mesh->elemlen = cfg->elemlen;

        mesh->elem = (int*)malloc(sizeof(int) * mesh->elemlen * mesh->ne);
        mesh->type = (int*)malloc(sizeof(int ) * mesh->ne);

        datalen = (cfg->method == rtBLBadouelGrid) ? cfg->crop0.z : ( (cfg->basisorder) ? mesh->nn : mesh->ne);
        mesh->weight = (double*)calloc(sizeof(double) * datalen, cfg->maxgate * cfg->srcnum);

        for (i = 0; i < mesh->ne; i++) {
            for (j = 0; j < mesh->elemlen; j++) {
                mesh->elem[i * mesh->elemlen + j] = cfg->elem[i * (mesh->elemlen + 1) + j];
            }

            mesh->type[i] = cfg->elem[i * (mesh->elemlen + 1) + mesh->elemlen];
        }

        mesh_srcdetelem(mesh, cfg);
        return;
    }

    mesh_filenames("elem_%s.dat", felem, cfg);

    if ((fp = fopen(felem, "rt")) == NULL) {
        MESH_ERROR("can not open element file");
    }

    len = fscanf(fp, "%d %d", &(mesh->elemlen), &(mesh->ne));

    if (len != 2 || mesh->ne <= 0) {
        MESH_ERROR("element file has wrong format");
    }

    if (mesh->elemlen < 4) {
        mesh->elemlen = 4;
    }

    mesh->elem = (int*)malloc(sizeof(int) * mesh->elemlen * mesh->ne);
    mesh->type = (int*)malloc(sizeof(int ) * mesh->ne);

    datalen = (cfg->method == rtBLBadouelGrid) ? cfg->crop0.z : ( (cfg->basisorder) ? mesh->nn : mesh->ne);
    mesh->weight = (double*)calloc(sizeof(double) * datalen, cfg->maxgate * cfg->srcnum);

    for (i = 0; i < mesh->ne; i++) {
        pe = mesh->elem + i * mesh->elemlen;

        if (fscanf(fp, "%d", &tmp) != 1) {
            break;
        }

        for (j = 0; j < mesh->elemlen; j++)
            if (fscanf(fp, "%d", pe + j) != 1) {
                break;
            }

        if (fscanf(fp, "%d", mesh->type + i) != 1) {
            break;
        }
    }

    fclose(fp);

    if (i < mesh->ne) {
        MESH_ERROR("element file has wrong format");
    }

    mesh_srcdetelem(mesh, cfg);
}

/**
 * @brief Load tet element volume file and initialize the related mesh properties
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_loadelemvol(tetmesh* mesh, mcconfig* cfg) {
    FILE* fp;
    int tmp, len, i, j, *ee;
    char fvelem[MAX_FULL_PATH];

    mesh_filenames("velem_%s.dat", fvelem, cfg);

    if ((cfg->elem && cfg->elemnum && cfg->node && cfg->nodenum) || (fp = fopen(fvelem, "rt")) == NULL) {
        mesh_getvolume(mesh, cfg);
        return;
    }

    len = fscanf(fp, "%d %d", &tmp, &(mesh->ne));

    if (len != 2 || mesh->ne <= 0) {
        MESH_ERROR("mesh file has wrong format");
    }

    mesh->evol = (float*)malloc(sizeof(float) * mesh->ne);
    mesh->nvol = (float*)calloc(sizeof(float), mesh->nn);

    for (i = 0; i < mesh->ne; i++) {
        if (fscanf(fp, "%d %f", &tmp, mesh->evol + i) != 2) {
            MESH_ERROR("mesh file has wrong format");
        }

        if (mesh->type[i] == 0) {
            continue;
        }

        ee = (int*)(mesh->elem + i * mesh->elemlen);

        for (j = 0; j < mesh->elemlen; j++) {
            mesh->nvol[ee[j] - 1] += mesh->evol[i] * 0.25f;
        }
    }

    fclose(fp);
}


/**
 * @brief Load face-neightbor element list and initialize the related mesh properties
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_loadfaceneighbor(tetmesh* mesh, mcconfig* cfg) {
    FILE* fp;
    int len, i, j;
    int* pe;
    char ffacenb[MAX_FULL_PATH];

    mesh_filenames("facenb_%s.dat", ffacenb, cfg);

    if ((cfg->elem && cfg->elemnum) || (fp = fopen(ffacenb, "rt")) == NULL) {
        mesh_getfacenb(mesh, cfg);
        return;
    }

    len = fscanf(fp, "%d %d", &(mesh->elemlen), &(mesh->ne));

    if (len != 2 || mesh->ne <= 0) {
        MESH_ERROR("mesh file has wrong format");
    }

    if (mesh->elemlen < 4) {
        mesh->elemlen = 4;
    }

    mesh->facenb = (int*)malloc(sizeof(int) * mesh->elemlen * mesh->ne);

    for (i = 0; i < mesh->ne; i++) {
        pe = mesh->facenb + i * mesh->elemlen;

        for (j = 0; j < mesh->elemlen; j++)
            if (fscanf(fp, "%d", pe + j) != 1) {
                MESH_ERROR("face-neighbor list file has wrong format");
            }
    }

    fclose(fp);
}

/**
 * @brief Load previously saved photon seeds from an .mch file for replay
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */

void mesh_loadseedfile(tetmesh* mesh, mcconfig* cfg) {
    history his;
    FILE* fp = fopen(cfg->seedfile, "rb");

    if (fp == NULL) {
        MESH_ERROR("can not open the specified history file");
    }

    if (fread(&his, sizeof(history), 1, fp) != 1) {
        MESH_ERROR("error when reading the history file");
    }

    if (his.savedphoton == 0 || his.seedbyte == 0) {
        fclose(fp);
        return;
    }

    if (his.maxmedia != mesh->prop) {
        MESH_ERROR("the history file was generated with a different media setting");
    }

    if (fseek(fp, his.savedphoton * his.colcount * sizeof(float), SEEK_CUR)) {
        MESH_ERROR("illegal history file");
    }

    cfg->photonseed = malloc(his.savedphoton * his.seedbyte);

    if (cfg->photonseed == NULL) {
        MESH_ERROR("can not allocate memory");
    }

    if (fread(cfg->photonseed, his.seedbyte, his.savedphoton, fp) != his.savedphoton) {
        MESH_ERROR("error when reading the seed data");
    }

    cfg->seed = SEED_FROM_FILE;
    cfg->nphoton = his.savedphoton;

    if (cfg->outputtype == otJacobian || cfg->outputtype == otWL || cfg->outputtype == otWP || cfg->replaydet > 0) {
        int i, j;
        float* ppath = (float*)malloc(his.savedphoton * his.colcount * sizeof(float));
        cfg->replayweight = (float*)malloc(his.savedphoton * sizeof(float));
        cfg->replaytime = (float*)malloc(his.savedphoton * sizeof(float));
        fseek(fp, sizeof(his), SEEK_SET);

        if (fread(ppath, his.colcount * sizeof(float), his.savedphoton, fp) != his.savedphoton) {
            MESH_ERROR("error when reading the partial path data");
        }

        cfg->nphoton = 0;

        for (i = 0; i < his.savedphoton; i++)
            if (cfg->replaydet == 0 || cfg->replaydet == (int)(ppath[i * his.colcount])) {
                memcpy((char*)(cfg->photonseed) + cfg->nphoton * his.seedbyte, (char*)(cfg->photonseed) + i * his.seedbyte, his.seedbyte);

                // replay with wide-field detection pattern, the partial path has to contain photon exit information
                if ((cfg->detparam1.w * cfg->detparam2.w > 0) && (cfg->detpattern != NULL)) {
                    cfg->replayweight[cfg->nphoton] = mesh_getdetweight(i, his.colcount, ppath, cfg);
                } else {
                    cfg->replayweight[cfg->nphoton] = ppath[(i + 1) * his.colcount - 1];
                }

                for (j = 2; j < his.maxmedia + 2; j++) {
                    cfg->replayweight[cfg->nphoton] *= expf(-mesh->med[j - 1].mua * ppath[i * his.colcount + j] * his.unitinmm);
                }

                cfg->replaytime[cfg->nphoton] = 0.f;

                for (j = 2; j < his.maxmedia + 2; j++) {
                    cfg->replaytime[cfg->nphoton] += mesh->med[j - 1].n * ppath[i * his.colcount + j] * R_C0;
                }

                cfg->nphoton++;
            }

        free(ppath);
        cfg->photonseed = realloc(cfg->photonseed, cfg->nphoton * his.seedbyte);
        cfg->replayweight = (float*)realloc(cfg->replayweight, cfg->nphoton * sizeof(float));
        cfg->replaytime = (float*)realloc(cfg->replaytime, cfg->nphoton * sizeof(float));
        cfg->minenergy = 0.f;
    }

    fclose(fp);
}

#endif

/**
 * @brief Compute the tetrahedron and nodal volumes if not provided
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 */


void mesh_getvolume(tetmesh* mesh, mcconfig* cfg) {
    float dx, dy, dz;
    int i, j;

    mesh->evol = (float*)calloc(sizeof(float), mesh->ne);
    mesh->nvol = (float*)calloc(sizeof(float), mesh->nn);

    for (i = 0; i < mesh->ne; i++) {
        int* ee = (int*)(mesh->elem + i * mesh->elemlen);

        dx = mesh->node[ee[2] - 1].x - mesh->node[ee[3] - 1].x;
        dy = mesh->node[ee[2] - 1].y - mesh->node[ee[3] - 1].y;
        dz = mesh->node[ee[2] - 1].z - mesh->node[ee[3] - 1].z;

        mesh->evol[i] = mesh->node[ee[1] - 1].x * (mesh->node[ee[2] - 1].y * mesh->node[ee[3] - 1].z - mesh->node[ee[2] - 1].z * mesh->node[ee[3] - 1].y)
                        - mesh->node[ee[1] - 1].y * (mesh->node[ee[2] - 1].x * mesh->node[ee[3] - 1].z - mesh->node[ee[2] - 1].z * mesh->node[ee[3] - 1].x)
                        + mesh->node[ee[1] - 1].z * (mesh->node[ee[2] - 1].x * mesh->node[ee[3] - 1].y - mesh->node[ee[2] - 1].y * mesh->node[ee[3] - 1].x);
        mesh->evol[i] += -mesh->node[ee[0] - 1].x * ((mesh->node[ee[2] - 1].y * mesh->node[ee[3] - 1].z - mesh->node[ee[2] - 1].z * mesh->node[ee[3] - 1].y) + mesh->node[ee[1] - 1].y * dz - mesh->node[ee[1] - 1].z * dy);
        mesh->evol[i] += +mesh->node[ee[0] - 1].y * ((mesh->node[ee[2] - 1].x * mesh->node[ee[3] - 1].z - mesh->node[ee[2] - 1].z * mesh->node[ee[3] - 1].x) + mesh->node[ee[1] - 1].x * dz - mesh->node[ee[1] - 1].z * dx);
        mesh->evol[i] += -mesh->node[ee[0] - 1].z * ((mesh->node[ee[2] - 1].x * mesh->node[ee[3] - 1].y - mesh->node[ee[2] - 1].y * mesh->node[ee[3] - 1].x) + mesh->node[ee[1] - 1].x * dy - mesh->node[ee[1] - 1].y * dx);
        mesh->evol[i] = -mesh->evol[i];

        if (mesh->evol[i] < 0.f) {
            int e1 = ee[3];
            ee[3] = ee [2];
            ee[2] = e1;
            mesh->evol[i] = -mesh->evol[i];
        }

        mesh->evol[i] *= (1.f / 6.f);

        if (mesh->type[i] == 0) {
            continue;
        }

        for (j = 0; j < mesh->elemlen; j++) {
            mesh->nvol[ee[j] - 1] += mesh->evol[i] * 0.25f;
        }
    }
}

/**
 * @brief Scan all tetrahedral elements to find the one enclosing the source
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration structure
 *
 * @return 0 if an enclosing element is found, 1 if not found
 */

int mesh_initelem(tetmesh* mesh, mcconfig* cfg) {
    FLOAT3* nodes = mesh->node;
    int i, j;

    for (i = 0; i < mesh->ne; i++) {
        double pmin[3] = {VERY_BIG, VERY_BIG, VERY_BIG}, pmax[3] = {-VERY_BIG, -VERY_BIG, -VERY_BIG};
        int* elems = (int*)(mesh->elem + i * mesh->elemlen); // convert int4* to int*

        for (j = 0; j < mesh->elemlen; j++) {
            pmin[0] = MIN(nodes[elems[j] - 1].x, pmin[0]);
            pmin[1] = MIN(nodes[elems[j] - 1].y, pmin[1]);
            pmin[2] = MIN(nodes[elems[j] - 1].z, pmin[2]);

            pmax[0] = MAX(nodes[elems[j] - 1].x, pmax[0]);
            pmax[1] = MAX(nodes[elems[j] - 1].y, pmax[1]);
            pmax[2] = MAX(nodes[elems[j] - 1].z, pmax[2]);
        }

        if (cfg->srcpos.x <= pmax[0] && cfg->srcpos.x >= pmin[0] &&
                cfg->srcpos.y <= pmax[1] && cfg->srcpos.y >= pmin[1] &&
                cfg->srcpos.z <= pmax[2] && cfg->srcpos.z >= pmin[2]) {

            if (mesh_barycentric(i + 1, &(cfg->bary0.x), (FLOAT3*) & (cfg->srcpos), mesh) == 0) {
                cfg->e0 = i + 1;
                return 0;
            }
        }
    }

    return 1;
}


/**
 * @brief Compute the barycentric coordinate of the source in the initial element
 *
 * @param[in] e0: the index (start from 1) of the initial element
 * @param[in] bary: the index (start from 1) of the initial element
 * @param[in] srcpos: the source position
 * @param[in] mesh: the mesh of the domain
 *
 * @return 0 if all barycentric coordinates are computed and all positive, or 1 any of those is negative
 */

int mesh_barycentric(int e0, float* bary, FLOAT3* srcpos, tetmesh* mesh) {
    int eid = e0 - 1, i;
    FLOAT3 vecS = {0.f}, vecAB, vecAC, vecN;
    FLOAT3* nodes = mesh->node;
    int ea, eb, ec;
    float s = 0.f;
    int* elems = (int*)(mesh->elem + eid * mesh->elemlen); // convert int4* to int*

    if (eid >= mesh->ne) {
        MESH_ERROR("initial element index exceeds total element count");
    }

    for (i = 0; i < 4; i++) {
        ea = elems[out[i][0]] - 1;
        eb = elems[out[i][1]] - 1;
        ec = elems[out[i][2]] - 1;
        vec_diff3(&nodes[ea], &nodes[eb], &vecAB);
        vec_diff3(&nodes[ea], &nodes[ec], &vecAC);
        vec_diff3(&nodes[ea], srcpos, &vecS);
        vec_cross3(&vecAB, &vecAC, &vecN);
        bary[facemap[i]] = -vec_dot3(&vecS, &vecN);
    }

    for (i = 0; i < 4; i++) {
        if (bary[i] < 0.f) {
            return 1;
        }

        s += bary[i];
    }

    for (i = 0; i < 4; i++) {
        bary[i] /= s;
    }

    return 0;
}

/**
 * @brief Initialize a data structure storing all pre-computed ray-tracing related data
 *
 * the pre-computed ray-tracing data include
 * d: the vector of the 6 edges
 * m: the cross-product of the end-nodes of the 6-edges n1 x n2
 * n: the normal vector of the 4 facets, pointing outwards
 *
 * @param[out] tracer: the ray-tracer data structure
 * @param[in] pmesh: the mesh object
 * @param[in] methodid: the ray-tracing algorithm to be used
 */

void tracer_init(raytracer* tracer, tetmesh* pmesh, char methodid) {
    tracer->d = NULL;
    tracer->m = NULL;
    tracer->n = NULL;
    tracer->mesh = pmesh;
    tracer->method = methodid;
    tracer_build(tracer);
}


/**
 * @brief Preparing for the ray-tracing calculations
 *
 * This function build the ray-tracing precomputed data and test if the initial
 * element (e0) encloses the photon launch position.
 *
 * @param[out] tracer: the ray-tracer data structure
 * @param[in] cfg: the simulation configuration structure
 */

void tracer_prep(raytracer* tracer, mcconfig* cfg) {
    int i, j, k, ne = tracer->mesh->ne;

    if (tracer->n == NULL && tracer->m == NULL && tracer->d == NULL) {
        if (tracer->mesh != NULL) {
            tracer_build(tracer);
        } else {
            MESH_ERROR("tracer is not associated with a mesh");
        }
    } else if ( (cfg->srctype == stPencil || cfg->srctype == stIsotropic || cfg->srctype == stCone || cfg->srctype == stArcSin) ) {
        if (cfg->e0 <= 0 || mesh_barycentric(cfg->e0, &cfg->bary0.x, (FLOAT3*) & (cfg->srcpos), tracer->mesh)) {
            if (mesh_initelem(tracer->mesh, cfg)) {
                MESH_ERROR("initial element does not enclose the source!");
            }
        }

        if (cfg->debuglevel & dlWeight)
            fprintf(cfg->flog, "initial bary-centric volumes [%e %e %e %e]\n",
                    cfg->bary0.x / 6., cfg->bary0.y / 6., cfg->bary0.z / 6., cfg->bary0.w / 6.);
    }

    // TODO: this is a partial fix to the normalization bug described in https://github.com/fangq/mmc/issues/82
    // basically, mmc surface node fluence is about 2x higher than expected, due to the division to the nodal-volume
    // of surface nodes that is roughtly half of that in an interior node.
    // To more carefully handle this, one should calculate the solid-angle S of any surface nodes and use the
    // ratio (4*Pi/S) to scale nvol. Here, we simply multiply surface node nvol by 2x. Should work fine for
    // flat surfaces, but will not be accurate for edge or corner nodes.
    // one can disable this fix (i.e. restore to the old behavior) by setting cfg.isnormalized to 2

    if (cfg->isnormalized == 1 && cfg->method != rtBLBadouelGrid && cfg->basisorder) {
        float* Reff = (float*)calloc(tracer->mesh->prop + 1, sizeof(float));

        if (cfg->isreflect) {
            for (i = 1; i <= tracer->mesh->prop; i++) { // precompute the Reff for each non-zero label
                for (j = 1; j < i; j++) {
                    if (tracer->mesh->med[j].n == tracer->mesh->med[i].n) { // if such reflective index has been computed, copy the value
                        Reff[i] = Reff[j];
                        break;
                    }
                }

                if (Reff[i] == 0.f) {
                    Reff[i] = mesh_getreff(tracer->mesh->med[i].n, tracer->mesh->med[0].n);
                }
            }
        }

        for (i = 0; i < ne; i++) {
            int* elems = tracer->mesh->elem + i * tracer->mesh->elemlen;   // element node indices
            int* enb = tracer->mesh->facenb + i * tracer->mesh->elemlen;   // check facenb, find surface faces

            for (j = 0; j < tracer->mesh->elemlen; j++) { // loop over my neighbors
                if (enb[j] == 0) {                        // 0-valued face neighbor indicates an exterior triangle
                    for (k = 0; k < 3; k++) {
                        int nid = elems[out[ifaceorder[j]][k]] - 1;

                        if (tracer->mesh->nvol[nid] > 0.f && tracer->mesh->type[i] >= 0) {  // change sign to prevent it from changing again
                            tracer->mesh->nvol[nid] *= -(2.f / (1.0 + Reff[tracer->mesh->type[i]])); // 2 accounts for the missing half of the solid angle, Reff is the effective reflection coeff
                        }
                    }
                }
            }
        }

        free(Reff);

        for (i = 0; i < tracer->mesh->nn; i++) {
            if (tracer->mesh->nvol[i] < 0.f) {
                tracer->mesh->nvol[i] = -tracer->mesh->nvol[i];
            }
        }
    }

    // build acceleration data structure to speed up first-neighbor immc edge-roi calculation
    // loop over each edgeroi, count how many roi in each elem, and write to the first elem as negative integer
    if (tracer->mesh->edgeroi) {
        for (i = 0; i < ne; i++) {
            int count = 0;

            for (j = 0; j < 6; j++)
                if (tracer->mesh->edgeroi[(i * 6) + j] > 0.f) {
                    count++;
                }

            if (count && fabs(tracer->mesh->edgeroi[i * 6]) < EPS) {
                tracer->mesh->edgeroi[i * 6] = -count;    // number -1 to -6 indicates how many faces have ROIs
            }
        }

        for (i = 0; i < ne; i++) {
            if (fabs(tracer->mesh->edgeroi[i * 6]) < EPS) { // if I don't have roi
                for (j = 0; j < tracer->mesh->elemlen; j++) { // loop over my neighbors
                    int id = tracer->mesh->facenb[i * tracer->mesh->elemlen + j]; // loop over neighboring elements

                    if (id > 0 && fabs(tracer->mesh->edgeroi[(id - 1) * 6]) > EPS) { // if I don't have roi, but neighbor has, set ref id as -elemid-6, only handle 1 roi neighbor case
                        tracer->mesh->edgeroi[i * 6] = -id - 6;
                        break;
                    }
                }
            }

            if (fabs(tracer->mesh->edgeroi[i * 6]) < EPS) { // if I don't have roi
                for (j = 0; j < tracer->mesh->elemlen; j++) { // loop over my neighbors
                    int firstnbid = tracer->mesh->facenb[i * tracer->mesh->elemlen + j] - 1; // loop over neighboring elements

                    if (firstnbid < 0) {
                        continue;
                    }

                    for (k = 0; k < tracer->mesh->elemlen; k++) { // loop over my j-th neighbor's neighbors
                        int id = tracer->mesh->facenb[firstnbid * tracer->mesh->elemlen + k]; // loop over 2nd order neighboring elements

                        if (id > 0 && fabs(tracer->mesh->edgeroi[(id - 1) * 6]) > EPS) { // if I don't have roi, but neighbor has, set ref id as -elemid-6, only handle 1 roi neighbor case
                            tracer->mesh->edgeroi[i * 6] = -id - 6;
                            break;
                        }
                    }
                }
            }
        }
    }

    // build acceleration data structure to speed up first-neighbor immc face-roi calculation
    // loop over each faceroi, count how many roi in each elem, and write to the first elem as negative integer
    if (tracer->mesh->faceroi) {
        for (i = 0; i < ne; i++) {
            int count = 0;

            for (j = 0; j < 4; j++)
                if (tracer->mesh->faceroi[(i << 2) + j] > 0.f) {
                    count++;
                }

            if (count && fabs(tracer->mesh->faceroi[i << 2]) < EPS) {
                tracer->mesh->faceroi[i << 2] = -count;    // number -1 to -4 indicates how many faces have ROIs
            }
        }

        for (i = 0; i < ne; i++) {
            if (fabs(tracer->mesh->faceroi[i << 2]) < EPS) { // if I don't have roi
                for (j = 0; j < tracer->mesh->elemlen; j++) { // loop over my neighbors
                    int id = tracer->mesh->facenb[i * tracer->mesh->elemlen + j]; // loop over neighboring elements

                    if (id > 0 && fabs(tracer->mesh->faceroi[(id - 1) << 2]) > EPS) { // if I don't have roi, but neighbor has, set ref id as -elemid-4, only handle 1 roi neighbor case
                        tracer->mesh->faceroi[i << 2] = -id - 4;
                        break;
                    }
                }
            }
        }
    }

    // loop over each external surface triangle (facenb[]==0) and sequentially number them as negative integer
    ne = tracer->mesh->ne * tracer->mesh->elemlen;
    tracer->mesh->nf = 0;

    for (i = 0; i < ne; i++) {
        if (tracer->mesh->facenb[i] == 0) {
            tracer->mesh->facenb[i] = -(++tracer->mesh->nf);
        }
    }

    if (tracer->mesh->dref) {
        free(tracer->mesh->dref);
    }

    if (cfg->issaveref) {
        tracer->mesh->dref = (double*)calloc(sizeof(double) * tracer->mesh->nf * cfg->srcnum, cfg->maxgate);
    }
}

/**
 * @brief Pre-computed ray-tracing related data
 *
 * the pre-computed ray-tracing data include
 * d: the vector of the 6 edges: edge[i]=[ni1,ni2]: d[i]=ni2 - ni1
 * m: the cross-product of the end-nodes of the 6-edges: edge[i]=[ni1,ni2]: m[i]=ni1 x ni2
 * n: the normal vector of the 4 facets, pointing outwards
 *
 * @param[out] tracer: the ray-tracer data structure
 */

void tracer_build(raytracer* tracer) {
    int ne, i, j;
    const int pairs[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};

    FLOAT3* nodes;
    int* elems, ebase;
    int e1, e0;
    float Rn2;

    if (tracer->d || tracer->m || tracer->n || tracer->mesh == NULL) {
        return;
    }

    if (tracer->mesh->node == NULL || tracer->mesh->elem == NULL ||
            tracer->mesh->facenb == NULL || tracer->mesh->med == NULL) {
        MESH_ERROR("mesh is missing");
    }

    ne = tracer->mesh->ne;
    nodes = tracer->mesh->node;
    elems = (int*)(tracer->mesh->elem); // convert int4* to int*

    if (tracer->method == rtPlucker) {
        int ea, eb, ec;
        FLOAT3 vecAB = {0.f}, vecAC = {0.f};

        tracer->d = (float3*)calloc(sizeof(float3), ne * 6); // 6 edges/elem
        tracer->m = (float3*)calloc(sizeof(float3), ne * 6); // 6 edges/elem
        tracer->n = (float3*)calloc(sizeof(float3), ne * 4); // 4 face norms

        for (i = 0; i < ne; i++) {
            ebase = i << 2;

            for (j = 0; j < 6; j++) {
                e1 = elems[ebase + pairs[j][1]] - 1;
                e0 = elems[ebase + pairs[j][0]] - 1;
                vec_diff3(&nodes[e0], &nodes[e1], (FLOAT3*)(tracer->d + i * 6 + j));
                vec_cross3(&nodes[e0], &nodes[e1], (FLOAT3*)(tracer->m + i * 6 + j));
            }

            for (j = 0; j < 4; j++) {
                ea = elems[ebase + out[j][0]] - 1;
                eb = elems[ebase + out[j][1]] - 1;
                ec = elems[ebase + out[j][2]] - 1;
                vec_diff3(&nodes[ea], &nodes[eb], &vecAB);
                vec_diff3(&nodes[ea], &nodes[ec], &vecAC);
                vec_cross3(&vecAB, &vecAC, (FLOAT3*)(tracer->n + ebase + j));
                Rn2 = 1.f / sqrt(vec_dot(tracer->n + ebase + j, tracer->n + ebase + j));
                vec_mult(tracer->n + ebase + j, Rn2, tracer->n + ebase + j);
            }
        }
    } else if (tracer->method == rtHavel || tracer->method == rtBadouel) {
        int ea, eb, ec;
        float3 vecAB = {0.f}, vecAC = {0.f};

        tracer->d = NULL;
        tracer->m = (float3*)calloc(sizeof(float3), ne * 12);

        for (i = 0; i < ne; i++) {
            ebase = i << 2;

            for (j = 0; j < 4; j++) {
                float3* vecN = tracer->m + 3 * (ebase + j);

                ea = elems[ebase + out[j][0]] - 1;
                eb = elems[ebase + out[j][1]] - 1;
                ec = elems[ebase + out[j][2]] - 1;
                vec_diff3(&nodes[ea], &nodes[eb], (FLOAT3*)&vecAB);
                vec_diff3(&nodes[ea], &nodes[ec], (FLOAT3*)&vecAC);
                vec_cross(&vecAB, &vecAC, vecN); /*N is defined as ACxAB in Jiri's code, but not the paper*/
                vec_cross(&vecAC, vecN, vecN + 1);
                vec_cross(vecN, &vecAB, vecN + 2);

                Rn2 = 1.f / sqrt(vec_dot(vecN, vecN));

                vec_mult(vecN, Rn2, vecN);

                Rn2 *= Rn2;
                vec_mult(vecN + 1, Rn2, vecN + 1);
                vec_mult(vecN + 2, Rn2, vecN + 2);
#if defined(MMC_USE_SSE) || defined(USE_OPENCL)
                vecN->w    = vec_dot3((FLOAT3*)vecN,  &nodes[ea]);
                (vecN + 1)->w = -vec_dot3((FLOAT3*)(vecN + 1), &nodes[ea]);
                (vecN + 2)->w = -vec_dot3((FLOAT3*)(vecN + 2), &nodes[ea]);
#endif
            }
        }
    } else if (tracer->method == rtBLBadouel || tracer->method == rtBLBadouelGrid) {
        int ea, eb, ec;
        float3 vecAB = {0.f}, vecAC = {0.f}, vN = {0.f};

        tracer->d = NULL;
        tracer->n = (float3*)calloc(sizeof(float3), ne * 4);

        for (i = 0; i < ne; i++) {
            ebase = i << 2;
            float* vecN = &(tracer->n[ebase].x);

            for (j = 0; j < 4; j++) {
                ea = elems[ebase + out[j][0]] - 1;
                eb = elems[ebase + out[j][1]] - 1;
                ec = elems[ebase + out[j][2]] - 1;
                vec_diff3(&nodes[ea], &nodes[eb], (FLOAT3*)&vecAB);
                vec_diff3(&nodes[ea], &nodes[ec], (FLOAT3*)&vecAC);

                vec_cross(&vecAB, &vecAC, &vN); /*N is defined as ACxAB in Jiri's code, but not the paper*/

                Rn2 = 1.f / sqrt(vec_dot(&vN, &vN));
                vec_mult(&vN, Rn2, &vN);
                vecN[j] = vN.x;
                vecN[j + 4] = vN.y;
                vecN[j + 8] = vN.z;
#if defined(MMC_USE_SSE) || defined(USE_OPENCL)
                vecN[j + 12]    = vec_dot3((FLOAT3*)&vN, &nodes[ea]);
#endif
            }
        }
    }
}

/**
 * @brief Clear the ray-tracing data structure
 *
 * Deconstructor of the ray-tracing data structure
 *
 * @param[out] tracer: the ray-tracer data structure
 */

void tracer_clear(raytracer* tracer) {
    if (tracer->d) {
        free(tracer->d);
        tracer->d = NULL;
    }

    if (tracer->m) {
        free(tracer->m);
        tracer->m = NULL;
    }

    if (tracer->n) {
        free(tracer->n);
        tracer->n = NULL;
    }

    tracer->mesh = NULL;
}

/**
 * @brief Performing one scattering event of the photon
 *
 * This function updates the direction of the photon by performing a scattering calculation
 *
 * @param[in] g: anisotropy g
 * @param[out] dir: current ray direction vector
 * @param[out] ran: random number generator states
 * @param[out] ran0: additional random number generator states
 * @param[out] cfg: the simulation configuration
 * @param[out] pmom: buffer to store momentum transfer data if needed
 */

float mc_next_scatter(float g, float3* dir, RandType* ran, RandType* ran0, mcconfig* cfg, float* pmom) {

    float nextslen;
    float sphi = 0.f, cphi = 0.f, tmp0, theta, stheta, ctheta, tmp1;
    float3 p;

    rand_need_more(ran, ran0);

    //random scattering length (normalized)
#ifdef MMC_USE_SSE_MATH
    nextslen = rand_next_scatlen_ps(ran);
#else
    nextslen = rand_next_scatlen(ran);
#endif

    //random arimuthal angle
#ifdef MMC_USE_SSE_MATH
    rand_next_aangle_sincos(ran, &sphi, &cphi);
#else
    tmp0 = TWO_PI * rand_next_aangle(ran); //next arimuth angle
    mmc_sincosf(tmp0, &sphi, &cphi);
#endif

    //Henyey-Greenstein Phase Function, "Handbook of Optical Biomedical Diagnostics",2002,Chap3,p234
    //see Boas2002

    if (g > EPS) { //if g is too small, the distribution of theta is bad
        tmp0 = (1.f - g * g) / (1.f - g + 2.f * g * rand_next_zangle(ran));
        tmp0 *= tmp0;
        tmp0 = (1.f + g * g - tmp0) / (2.f * g);

        // when ran=1, CUDA will give me 1.000002 for tmp0 which produces nan later
        if (tmp0 > 1.f) {
            tmp0 = 1.f;
        }

        if (tmp0 < -1.f) {
            tmp0 = -1.f;
        }

        theta = acosf(tmp0);
        stheta = sqrt(1.f - tmp0 * tmp0);
        //stheta=sinf(theta);
        ctheta = tmp0;
    } else {
        theta = acosf(2.f * rand_next_zangle(ran) - 1.f);
        mmc_sincosf(theta, &stheta, &ctheta);
    }

    if ( dir->z > -1.f + EPS && dir->z < 1.f - EPS ) {
        tmp0 = 1.f - dir->z * dir->z; //reuse tmp to minimize registers
        tmp1 = 1.f / sqrtf(tmp0);
        tmp1 = stheta * tmp1;

        p.x = tmp1 * (dir->x * dir->z * cphi - dir->y * sphi) + dir->x * ctheta;
        p.y = tmp1 * (dir->y * dir->z * cphi + dir->x * sphi) + dir->y * ctheta;
        p.z = -tmp1 * tmp0 * cphi             + dir->z * ctheta;
    } else {
        p.x = stheta * cphi;
        p.y = stheta * sphi;
        p.z = (dir->z > 0.f) ? ctheta : -ctheta;
    }

    if (cfg->ismomentum) {
        pmom[0] += (1.f - ctheta);
    }

    dir->x = p.x;
    dir->y = p.y;
    dir->z = p.z;
    return nextslen;
}

#ifndef MCX_CONTAINER

void mcx_savecamsignals(float* camsignals, size_t len, mcconfig* cfg)
{
    char* file_suffix = ".bin";
    size_t filepathlen = strlen(cfg->session) + strlen(file_suffix) + 1;
    char* filepath = malloc(filepathlen);

    strcpy(filepath, cfg->session);
    strcat(filepath, file_suffix);

    FILE *f = fopen(filepath, "wb");
    fwrite(camsignals, sizeof(float), len, f);
    fclose(f);
}

/**
 * @brief Save the fluence output to a file
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration
 */

void mesh_saveweight(tetmesh* mesh, mcconfig* cfg, int isref) {
    FILE* fp;
    int i, j, datalen = (cfg->method == rtBLBadouelGrid) ? cfg->crop0.z : ( (cfg->basisorder) ? mesh->nn : mesh->ne);
    char fweight[MAX_FULL_PATH];
    double* data = mesh->weight;

    if (isref) {
        data = mesh->dref;
        datalen = mesh->nf;
    }

    if (cfg->rootpath[0]) {
        sprintf(fweight, "%s%c%s%s.dat", cfg->rootpath, pathsep, cfg->session, (isref ? "_dref" : ""));
    } else {
        sprintf(fweight, "%s%s.dat", cfg->session, (isref ? "_dref" : ""));
    }

    if (cfg->outputformat >= ofBin && cfg->outputformat <= ofBJNifti) {
        uint3 dim0 = cfg->dim;

        if (cfg->method != rtBLBadouelGrid) {
            cfg->dim.x = cfg->srcnum;
            cfg->dim.y = cfg->maxgate;
            cfg->dim.z = datalen;
        }

        mcx_savedata(mesh->weight, datalen * cfg->maxgate * cfg->srcnum, cfg, isref);
        cfg->dim = dim0;
        return;
    }

    if ((fp = fopen(fweight, "wt")) == NULL) {
        MESH_ERROR("can not open weight file to write");
    }

    for (i = 0; i < cfg->maxgate; i++) {
        for (j = 0; j < datalen; j++) {
            if (1 == cfg->srcnum) {
                if (fprintf(fp, "%d\t%e\n", j + 1, data[i * datalen + j]) == 0) {
                    MESH_ERROR("can not write to weight file");
                }
            } else { // multiple sources for pattern illumination type
                int k, shift;

                for (k = 0; k < cfg->srcnum; k++) {
                    shift = (i * datalen + j) * cfg->srcnum + k;

                    if (fprintf(fp, "%d\t%d\t%e\n", j + 1, k + 1, data[shift]) == 0) {
                        MESH_ERROR("can not write to weight file");
                    }
                }
            }
        }
    }

    fclose(fp);
}

/**
 * @brief Save detected photon data into an .mch history file
 *
 * @param[in] ppath: buffer points to the detected photon data (partial-path, det id, etc)
 * @param[in] seeds: buffer points to the detected photon seeds
 * @param[in] count: how many photons are detected
 * @param[in] seedbyte: how many bytes per detected photon seed
 * @param[in] cfg: the simulation configuration
 */

void mesh_savedetphoton(float* ppath, void* seeds, int count, int seedbyte, mcconfig* cfg) {
    FILE* fp;
    char fhistory[MAX_FULL_PATH], filetag;

    filetag = ((cfg->his.detected == 0  && cfg->his.savedphoton) ? 't' : 'h');

    if (cfg->rootpath[0]) {
        sprintf(fhistory, "%s%c%s.mc%c", cfg->rootpath, pathsep, cfg->session, filetag);
    } else {
        sprintf(fhistory, "%s.mc%c", cfg->session, filetag);
    }

    if ((fp = fopen(fhistory, "wb")) == NULL) {
        MESH_ERROR("can not open history file to write");
    }

    cfg->his.unitinmm = 1.f;

    if (cfg->method != rtBLBadouelGrid) {
        cfg->his.unitinmm = cfg->unitinmm;
    }

    cfg->his.srcnum = cfg->srcnum;
    cfg->his.detnum = cfg->detnum;

    if (cfg->issaveseed && seeds != NULL) {
        cfg->his.seedbyte = seedbyte;
    }

    /*
        if (count > 0 && cfg->exportdetected == NULL) {
            cfg->detectedcount = count;
            cfg->exportdetected = (float*)malloc(cfg->his.colcount * cfg->detectedcount * sizeof(float));
        }

        if (cfg->exportdetected != ppath) {
            memcpy(cfg->exportdetected, ppath, count * cfg->his.colcount * sizeof(float));
        }
    */
    //fwrite(&(cfg->his), sizeof(history), 1, fp);
    fwrite(ppath, sizeof(float), count * cfg->his.colcount, fp);

    //if (cfg->issaveseed && seeds != NULL) {
    //    fwrite(seeds, seedbyte, count, fp);
    //}

    fclose(fp);
}

#endif

/**
 * @brief Save binned detected photon data over an area-detector as time-resolved 2D images
 *
 * When an area detector is used (such as a CCD), storing all detected photons can generate
 * a huge output file. This can be mitigated by accumulate the data first to a rasterized
 * detector grid, and then save only the integrated data.
 *
 * @param[out] detmap: buffer points to the output detector data array
 * @param[in] ppath: buffer points to the detected photon data (partial-path, det id, etc)
 * @param[in] count: how many photons are detected
 * @param[in] cfg: the simulation configuration
 * @param[in] mesh: the mesh object
 */

void mesh_getdetimage(float* detmap, float* ppath, int count, mcconfig* cfg, tetmesh* mesh) {
    // cfg->issaveexit is 2 for this mode
    int colcount = (2 + (cfg->ismomentum > 0)) * cfg->his.maxmedia + 6 + 2;
    float x0 = cfg->detpos[0].x;
    float y0 = cfg->detpos[0].y;
    float xrange = cfg->detparam1.x + cfg->detparam2.x;
    float yrange = cfg->detparam1.y + cfg->detparam2.y;
    int xsize = cfg->detparam1.w;
    int ysize = cfg->detparam2.w;
    int i, j, xindex, yindex, ntg, offset;
    float unitinmm = (cfg->method != rtBLBadouelGrid) ? cfg->his.unitinmm : 1.f;

    float xloc, yloc, weight, path;

    for (i = 0; i < count; i++) {
        path = 0;
        weight = ppath[(i + 1) * colcount - 1];

        for (j = 1; j <= cfg->his.maxmedia; j++) {
            path += ppath[i * colcount + j + cfg->his.maxmedia] * mesh->med[j].n;
            weight *= expf(-ppath[i * colcount + j + cfg->his.maxmedia] * mesh->med[j].mua * unitinmm);
        }

        ntg = (int) path * R_C0 / cfg->tstep;

        if (ntg > cfg->maxgate - 1) {
            ntg = cfg->maxgate - 1;
        }

        xloc = ppath[(i + 1) * colcount - 7];
        yloc = ppath[(i + 1) * colcount - 6];
        xindex = (xloc - x0) / xrange * xsize;

        if (xindex < 0 || xindex > xsize - 1) {
            continue;
        }

        yindex = (yloc - y0) / yrange * ysize;

        if (yindex < 0 || yindex > ysize - 1) {
            continue;
        }

        offset = ntg * xsize * ysize;
        detmap[offset + yindex * xsize + xindex] += weight;
    }
}

#ifndef MCX_CONTAINER

/**
 * @brief Save binned detected photon data over an area-detector
 *
 * function for saving binned detected photon data into time-resolved 2D images
 *
 * @param[in] detmap: buffer points to the output detector data array
 * @param[in] cfg: the simulation configuration
 */

void mesh_savedetimage(float* detmap, mcconfig* cfg) {

    FILE* fp;
    char fhistory[MAX_FULL_PATH];

    if (cfg->rootpath[0]) {
        sprintf(fhistory, "%s%c%s.img", cfg->rootpath, pathsep, cfg->session);
    } else {
        sprintf(fhistory, "%s.img", cfg->session);
    }

    if ((fp = fopen(fhistory, "wb")) == NULL) {
        MESH_ERROR("can not open detector image file to write");
    }

    fwrite(detmap, sizeof(float), cfg->detparam1.w * cfg->detparam2.w * cfg->maxgate, fp);
    fclose(fp);
}

#endif

/**
 * @brief Recompute the detected photon weight from the partial-pathlengths
 *
 * This function currently does not consider the final transmission coeff before
 * the photon being detected.
 *
 * @param[in] photonid: index of the detected photon
 * @param[in] colcount: how many 4-byte records per detected photon
 * @param[in] ppath: buffer points to the detected photon data (partial-path, det id, etc)
 * @param[in] cfg: the simulation configuration
 */

float mesh_getdetweight(int photonid, int colcount, float* ppath, mcconfig* cfg) {

    float x0 = cfg->detpos[0].x;
    float y0 = cfg->detpos[0].y;
    float xrange = cfg->detparam1.x + cfg->detparam2.x;
    float yrange = cfg->detparam1.y + cfg->detparam2.y;
    int xsize = cfg->detparam1.w;
    int ysize = cfg->detparam2.w;
    float xloc = ppath[(photonid + 1) * colcount - 7];
    float yloc = ppath[(photonid + 1) * colcount - 6];
    int xindex = (xloc - x0) / xrange * xsize;
    int yindex = (yloc - y0) / yrange * ysize;

    if (xindex < 0 || xindex > xsize - 1 || yindex < 0 || yindex > ysize - 1) {
        MESH_ERROR("photon location not within the detection plane");
    }

    return cfg->detpattern[yindex * xsize + xindex];
}

/**
 * @brief Function to normalize the fluence and remove influence from photon number and volume
 *
 * This function outputs the Green's function from the raw simulation data. This needs
 * to divide the total simulated photon energy, normalize the volumes of each node/elem,
 * and consider the length unit and time-gates
 *
 * @param[in] mesh: the mesh object
 * @param[in] cfg: the simulation configuration
 * @param[in] Eabsorb: total absorbed energy from ray-tracing accummulation
 * @param[in] Etotal: total launched energy, equal to photon number if not pattern-source
 */

/*see Eq (1) in Fang&Boas, Opt. Express, vol 17, No.22, pp. 20178-20190, Oct 2009*/
float mesh_normalize(tetmesh* mesh, mcconfig* cfg, float Eabsorb, float Etotal, int pair) {
    int i, j, k;
    double energydeposit = 0.f, energyelem, normalizor;
    int* ee;
    int datalen = (cfg->method == rtBLBadouelGrid) ? cfg->crop0.z : ( (cfg->basisorder) ? mesh->nn : mesh->ne);

    if (cfg->issaveref && mesh->dref) {
        float normalizor = 1.f / Etotal;

        for (i = 0; i < cfg->maxgate; i++)
            for (j = 0; j < mesh->nf; j++) {
                mesh->dref[i * mesh->nf + j] *= normalizor;
            }
    }

    if (cfg->seed == SEED_FROM_FILE && (cfg->outputtype == otJacobian || cfg->outputtype == otWL || cfg->outputtype == otWP)) {
        float normalizor = 1.f / (DELTA_MUA * cfg->nphoton);

        if (cfg->outputtype == otWL || cfg->outputtype == otWP) {
            normalizor = 1.f / Etotal;    /*Etotal is total detected photon weight in the replay mode*/
        }

        for (i = 0; i < cfg->maxgate; i++)
            for (j = 0; j < datalen; j++) {
                mesh->weight[(i * datalen + j)*cfg->srcnum + pair] *= normalizor;
            }

        return normalizor;
    }

    if (cfg->outputtype == otEnergy) {
        normalizor = 1.f / Etotal;

        for (i = 0; i < cfg->maxgate; i++)
            for (j = 0; j < datalen; j++) {
                mesh->weight[(i * datalen + j)*cfg->srcnum + pair] *= normalizor;
            }

        return normalizor;
    }

    if (cfg->method == rtBLBadouelGrid) {
        normalizor = 1.0 / (Etotal * cfg->unitinmm * cfg->unitinmm * cfg->unitinmm); /*scaling factor*/
    } else {
        if (cfg->basisorder) {
            for (i = 0; i < cfg->maxgate; i++)
                for (j = 0; j < datalen; j++)
                    if (mesh->nvol[j] > 0.f) {
                        mesh->weight[(i * datalen + j)*cfg->srcnum + pair] /= mesh->nvol[j];
                    }

            for (i = 0; i < mesh->ne; i++) {
                ee = (int*)(mesh->elem + i * mesh->elemlen);
                energyelem = 0.f;

                for (j = 0; j < cfg->maxgate; j++)
                    for (k = 0; k < 4; k++) {
                        energyelem += mesh->weight[(j * mesh->nn + ee[k] - 1) * cfg->srcnum + pair];    /*1/4 factor is absorbed two lines below*/
                    }

                energydeposit += energyelem * mesh->evol[i] * mesh->med[mesh->type[i]].mua; /**mesh->med[mesh->type[i]].n;*/
            }

            normalizor = Eabsorb / (Etotal * energydeposit * 0.25f); /*scaling factor*/
        } else {
            for (i = 0; i < datalen; i++)
                for (j = 0; j < cfg->maxgate; j++) {
                    energydeposit += mesh->weight[(j * datalen + i) * cfg->srcnum + pair];
                }

            for (i = 0; i < datalen; i++) {
                energyelem = mesh->evol[i] * mesh->med[mesh->type[i]].mua;

                for (j = 0; j < cfg->maxgate; j++) {
                    mesh->weight[(j * datalen + i)*cfg->srcnum + pair] /= energyelem;
                }
            }

            normalizor = Eabsorb / (Etotal * energydeposit); /*scaling factor*/
        }
    }

    if (cfg->outputtype == otFlux) {
        normalizor /= cfg->tstep;
    }

    for (i = 0; i < cfg->maxgate; i++)
        for (j = 0; j < datalen; j++) {
            mesh->weight[(i * datalen + j)*cfg->srcnum + pair] *= normalizor;
        }

    return normalizor;
}


/**
 * @brief Compute the effective reflection coefficient Reff using approximated formula
 *
 * accuracy is limited
 * see https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4482362/
 *
 * @param[out] tracer: the ray-tracer data structure
 * @param[in] pmesh: the mesh object
 * @param[in] methodid: the ray-tracing algorithm to be used
 */

double mesh_getreff_approx(double n_in, double n_out) {
    double nn = n_in / n_out;
    return -1.440 / (nn * nn) + 0.710 / nn + 0.668 + 0.0636 * nn;
}

/**
 * @brief Compute the effective reflection coefficient Reff
 *
 * @param[in] n_in: refractive index of the diffusive medium
 * @param[in] n_out: refractive index of the non-diffusive medium
 */

double mesh_getreff(double n_in, double n_out) {
    double oc = asin(1.0 / n_in); // critical angle
    const double count = 1000.0;
    const double ostep = (M_PI / (2.0 * count));
    double r_phi = 0.0, r_j = 0.0;
    double o, cosop, coso, r_fres, tmp;
    int i;

    for (i = 0; i < count; i++) {
        o = i * ostep;
        coso = cos(o);

        if (o < oc) {
            cosop = n_in * sin(o);
            cosop = sqrt(1. - cosop * cosop);
            tmp = (n_in * cosop - n_out * coso) / (n_in * cosop + n_out * coso);
            r_fres = 0.5 * tmp * tmp;
            tmp = (n_in * coso - n_out * cosop) / (n_in * coso + n_out * cosop);
            r_fres += 0.5 * tmp * tmp;
        } else {
            r_fres = 1.f;
        }

        r_phi += 2.0 * sin(o) * coso * r_fres;
        r_j += 3.0 * sin(o) * coso * coso * r_fres;
    }

    r_phi *= ostep;
    r_j *= ostep;
    return (r_phi + r_j) / (2.0 - r_phi + r_j);
}


/**
 * @brief Validate all input fields, and warn incompatible inputs
 *
 * Perform self-checking and raise exceptions or warnings when input error is detected
 *
 * @param[in,out] cfg: the simulation configuration structure
 * @param[out] mesh: the mesh data structure
 */

void mesh_validate(tetmesh* mesh, mcconfig* cfg) {
    int i, j, *ee, datalen;

    if (mesh->prop == 0) {
        MMC_ERROR(999, "you must define the 'prop' field in the input structure");
    }

    if (mesh->nn == 0 || mesh->ne == 0 || mesh->evol == NULL || mesh->facenb == NULL) {
        MMC_ERROR(999, "a complete input mesh include 'node','elem','facenb' and 'evol'");
    }

    if (mesh->node == NULL || mesh->elem == NULL || mesh->prop == 0) {
        MMC_ERROR(999, "You must define 'mesh' and 'prop' fields.");
    }

    if (mesh->nvol) {
        free(mesh->nvol);
    }

    mesh->nvol = (float*)calloc(sizeof(float), mesh->nn);

    for (i = 0; i < mesh->ne; i++) {
        if (mesh->type[i] <= 0) {
            continue;
        }

        ee = (int*)(mesh->elem + i * mesh->elemlen);

        for (j = 0; j < 4; j++) {
            mesh->nvol[ee[j] - 1] += mesh->evol[i] * 0.25f;
        }
    }

    if (mesh->weight) {
        free(mesh->weight);
    }

    if (cfg->method == rtBLBadouelGrid) {
        mesh_createdualmesh(mesh, cfg);
        cfg->basisorder = 0;
    }

    datalen = (cfg->method == rtBLBadouelGrid) ? cfg->crop0.z : ( (cfg->basisorder) ? mesh->nn : mesh->ne);
    mesh->weight = (double*)calloc(sizeof(double) * datalen * cfg->srcnum, cfg->maxgate);

    if (cfg->method != rtBLBadouelGrid && cfg->unitinmm != 1.f) {
        for (i = 1; i <= mesh->prop; i++) {
            mesh->med[i].mus *= cfg->unitinmm;
            mesh->med[i].mua *= cfg->unitinmm;
        }
    }

    /*make medianum+1 the same as medium 0*/
    if (cfg->isextdet) {
        mesh->med = (medium*)realloc(mesh->med, sizeof(medium) * (mesh->prop + 2));
        memcpy(mesh->med + mesh->prop + 1, mesh->med, sizeof(medium));

        for (i = 0; i < mesh->ne; i++) {
            if (mesh->type[i] == -2) {
                mesh->type[i] = mesh->prop + 1;
            }
        }
    }
}