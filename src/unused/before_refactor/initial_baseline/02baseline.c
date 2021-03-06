// matlab-based implementation as shown in meeting
#include <stdio.h>
#include <string.h>
#include <stdint.h>
//#include <stdint-gcc.h> Not available on RedHat6@CAB. Not needed.
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <float.h>
#include <tiffio.h>
#include "fusion.h"
#include "fusion_perfcost.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define CHANNELS 3

typedef struct {
    uint32_t r;
    uint32_t c;
    uint32_t npixels;
    uint32_t nvals;
    uint32_t nlev;
    uint32_t nimages;
    uint32_t max_upsampled_r;
    uint32_t max_upsampled_c;

    double *R;
    double **W;

    //result pyramid
    double **pyr;
    uint32_t *pyr_r;
    uint32_t *pyr_c;

    //pyrW[n] is pyramid of weight map for each of the N images
    double ***pyrW;
    uint32_t **pyrW_r;
    uint32_t **pyrW_c;

    //pyrI[n] is image pyramid for each of the N images
    double ***pyrI;
    uint32_t **pyrI_r;
    uint32_t **pyrI_c;

    //regular size scratch spaces
    double *tmp_weights;
    double *tmp2_weights;
    double *tmp_fullsize;
    double *tmp2_fullsize;
    double *tmp_halfsize;
    double *tmp_quartsize;
    double *tmp2_quartsize;
} segments_t;

int malloc_pyramid(uint32_t r, uint32_t c, uint32_t channels, uint32_t nlev, double ***pyr, uint32_t **pyr_r, uint32_t **pyr_c, bool level0_is_ref);
void free_pyramid(uint32_t nlev, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c, bool level0_is_ref);

void weights(uint32_t nimages, uint32_t npixels, uint32_t r, uint32_t c, double contrast_parm, double sat_parm, double wexp_parm, double **I, double **W, double *tmp_weights, double *tmp2_weights);
void pyramids(uint32_t nimages, uint32_t nlev, uint32_t r, uint32_t c, double **I, double **W,
              double *tmp_fullsize, double *tmp_halfsize, double *tmp_quartsize, double *tmp2_quartsize,
              double ***pyrW, uint32_t **pyrW_r, uint32_t **pyrW_c,
              double ***pyrI, uint32_t **pyrI_r, uint32_t **pyrI_c);
void blend(uint32_t nimages, uint32_t nlev,
           double **pyr, uint32_t *pyr_r, uint32_t *pyr_c,
           double ***pyrW, uint32_t **pyrW_r, uint32_t **pyrW_c,
           double ***pyrI, uint32_t **pyrI_r, uint32_t **pyrI_c);
void reconstruct_laplacian_pyramid(uint32_t channels, uint32_t nlev, double *tmp_fullsize, double *tmp2_fullsize, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c, uint32_t r, uint32_t c, double *dst);

void gaussian_pyramid(double *im, uint32_t r, uint32_t c, uint32_t channels, uint32_t nlev, double *tmp_halfsize, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c);
void laplacian_pyramid(double *im, uint32_t r, uint32_t c, uint32_t channels, uint32_t nlev, double *tmp_fullsize, double *tmp_halfsize, double *tmp_quartsize, double *tmp2_quartsize, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c);
void downsample(double *im, uint32_t r, uint32_t c, uint32_t channels, double *tmp_halfsize, uint32_t down_r, uint32_t down_c, double *dst);
void upsample(double *im, uint32_t r, uint32_t c, uint32_t channels, uint32_t up_r, uint32_t up_c, double *tmp_fullsize, double *dst);

void rgb2gray(double *im, size_t npixels, double* dst);
void conv3x3_monochrome_replicate(double* im, uint32_t r, uint32_t c, double* dst);

//
// Memory handling
//

int fusion_alloc(void** _segments, int w, int h, int N){

    segments_t *mem = malloc(sizeof(segments_t));
    if(mem == NULL){
        return FUSION_ALLOC_FAILURE;
    }

    mem->npixels = w*h;
    mem->nvals = w*h*3;
    mem->nlev = (uint32_t)(floor((log2(MIN(w,h)))));
    mem->nimages = N;
    mem->r = h;
    mem->c = w;
    mem->max_upsampled_r = h + (h%2);
    mem->max_upsampled_c = w + (w%2);

    mem->R = calloc(mem->nvals,sizeof(double));
    if(mem->R == NULL){
        return FUSION_ALLOC_FAILURE;
    }

    mem->W = calloc(mem->nimages,sizeof(double*));
    if(mem->W == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    for (int n = 0; n < mem->nimages; n++){
        mem->W[n] = calloc(mem->npixels,sizeof(double));
        if(mem->W[n] == NULL){
            return FUSION_ALLOC_FAILURE;
        }
    }

    mem->pyrW = calloc(mem->nimages,sizeof(double**));
    if(mem->pyrW == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->pyrW_r = calloc(mem->nimages,sizeof(double*));
    if(mem->pyrW_r == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->pyrW_c = calloc(mem->nimages,sizeof(double*));
    if(mem->pyrW_c == NULL){
        return FUSION_ALLOC_FAILURE;
    }

    mem->pyrI = calloc(mem->nimages,sizeof(double**));
    if(mem->pyrI == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->pyrI_r = calloc(mem->nimages,sizeof(double*));
    if(mem->pyrI_r == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->pyrI_c = calloc(mem->nimages,sizeof(double*));
    if(mem->pyrI_c == NULL){
        return FUSION_ALLOC_FAILURE;
    }

    if(malloc_pyramid(mem->r,mem->c,3,mem->nlev,&(mem->pyr), &(mem->pyr_r), &(mem->pyr_c),false) != FUSION_ALLOC_SUCCESS){
        return FUSION_ALLOC_FAILURE;
    }
    for (int n = 0; n < N; n++){
        if(malloc_pyramid(mem->r,mem->c,1,mem->nlev,&(mem->pyrW[n]), &(mem->pyrW_r[n]), &(mem->pyrW_c[n]),true) != FUSION_ALLOC_SUCCESS){
            return FUSION_ALLOC_FAILURE;
        }
    }
    for (int n = 0; n < N; n++){
        if(malloc_pyramid(mem->r,mem->c,3,mem->nlev,&(mem->pyrI[n]), &(mem->pyrI_r[n]), &(mem->pyrI_c[n]),false) != FUSION_ALLOC_SUCCESS){
            return FUSION_ALLOC_FAILURE;
        }
    }

    mem->tmp_weights = calloc(mem->npixels,sizeof(double));
    if(mem->tmp_weights == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->tmp2_weights = calloc(mem->npixels,sizeof(double));
    if(mem->tmp2_weights == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->tmp_fullsize = calloc(mem->max_upsampled_r * mem->max_upsampled_c * 3,sizeof(double));
    if(mem->tmp_fullsize == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->tmp2_fullsize = calloc(mem->max_upsampled_r * mem->max_upsampled_c * 3,sizeof(double));
    if(mem->tmp2_fullsize == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->tmp_halfsize = calloc(
                (mem->max_upsampled_r * mem->max_upsampled_c * 3)+1 / 2, sizeof(double));
    if(mem->tmp_halfsize == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->tmp_quartsize = calloc(
                ((mem->max_upsampled_r * mem->max_upsampled_c * 3)+1 / 2)+1 / 2, sizeof(double));
    if(mem->tmp_quartsize == NULL){
        return FUSION_ALLOC_FAILURE;
    }
    mem->tmp2_quartsize = calloc(
                ((mem->max_upsampled_r * mem->max_upsampled_c * 3)+1 / 2)+1 / 2, sizeof(double));
    if(mem->tmp2_quartsize == NULL){
        return FUSION_ALLOC_FAILURE;
    }

    *_segments = mem;

    return FUSION_ALLOC_SUCCESS;
}

/**
 * Allocate memory for the gaussian/laplacian pyramid at *pyr
 */
int malloc_pyramid(uint32_t r, uint32_t c, uint32_t channels, uint32_t nlev, double ***pyr, uint32_t **pyr_r, uint32_t **pyr_c, bool level0_is_ref){
    size_t pyr_len = nlev;
    *pyr = (double**) calloc(pyr_len,sizeof(double*));
    assert(*pyr != NULL);
    *pyr_r = (uint32_t*) calloc(nlev,sizeof(uint32_t));
    assert(*pyr_r != NULL);
    *pyr_c = (uint32_t*) calloc(nlev,sizeof(uint32_t));
    assert(*pyr_c != NULL);

    uint32_t r_level = r;
    uint32_t c_level = c;

    for(int i = 0; i < nlev; i++){
        (*pyr_r)[i] = r_level; //store dimension r at level i
        (*pyr_c)[i] = c_level; //store dimension c at level i

        if(i != 0 || !level0_is_ref){
            size_t L_len = r_level*c_level*channels;
            double* L = calloc(L_len,sizeof(double));
            if(L == NULL){
                return FUSION_ALLOC_FAILURE;
            }
            (*pyr)[i] = L; //add entry to array of pointers to image levels
        } else {
            (*pyr)[i] = NULL;
        }
        // for next level, width if odd: (W-1)/2+1, otherwise: (W-1)/2
        r_level = r_level / 2 + (r_level % 2);
        c_level = c_level / 2 + (c_level % 2);
    }
    return FUSION_ALLOC_SUCCESS;
}

void fusion_free( void* _segments ){
    segments_t *mem = _segments;

    free(mem->tmp_weights);
    free(mem->tmp2_weights);
    free(mem->tmp_fullsize);
    free(mem->tmp2_fullsize);
    free(mem->tmp_halfsize);
    free(mem->tmp_quartsize);
    free(mem->tmp2_quartsize);

    free_pyramid(mem->nlev,mem->pyr,mem->pyr_r,mem->pyr_c,false);

    for (int n = 0; n < mem->nimages; n++){
        free(mem->W[n]);
        free_pyramid(mem->nlev,mem->pyrI[n],mem->pyrI_r[n],mem->pyrI_c[n], false);
        free_pyramid(mem->nlev,mem->pyrW[n],mem->pyrW_r[n],mem->pyrW_c[n], true);
    }
    free(mem->pyrI);
    free(mem->pyrI_r);
    free(mem->pyrI_c);
    free(mem->pyrW);
    free(mem->pyrW_r);
    free(mem->pyrW_c);
    free(mem->W);
    free(mem->R);
    free(_segments);
}

void free_pyramid(uint32_t nlev, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c, bool level0_is_ref){
    for(int i = 0; i < nlev; i++){
        if(i != 0 || !level0_is_ref){
            free(pyr[i]);
        }
    }
    free(pyr_r);
    free(pyr_c);
    free(pyr);
}

//
// Exposure Fusion functionality
//

double* fusion_compute(double** I, double contrast_parm, double sat_parm, double wexp_parm,
                        void* _segments){
    segments_t *mem = _segments;

    uint32_t r = mem->r;
    uint32_t c = mem->c;
    uint32_t npixels = mem->npixels;
//    uint32_t nvals = mem->nvals;
    uint32_t nlev = mem->nlev;
    uint32_t nimages = mem->nimages;

    double *R = mem->R;
    double **W = mem->W;
    double **pyr = mem->pyr;
    uint32_t *pyr_r = mem->pyr_r;
    uint32_t *pyr_c = mem->pyr_c;
    double ***pyrW = mem->pyrW;
    uint32_t **pyrW_r = mem->pyrW_r;
    uint32_t **pyrW_c = mem->pyrW_c;
    double ***pyrI = mem->pyrI;
    uint32_t **pyrI_r = mem->pyrI_r;
    uint32_t **pyrI_c = mem->pyrI_c;

    double *tmp_weights = mem->tmp_weights;
    double *tmp2_weights = mem->tmp2_weights;
    double *tmp_fullsize = mem->tmp_fullsize;
    double *tmp2_fullsize = mem->tmp2_fullsize;
    double *tmp_halfsize = mem->tmp_halfsize;
    double *tmp_quartsize = mem->tmp_quartsize;
    double *tmp2_quartsize = mem->tmp2_quartsize;
PERF_FUNC_ENTER
    weights(nimages,npixels,r,c,contrast_parm,sat_parm,wexp_parm,I,W,tmp_weights,tmp2_weights);
PERF_FUNC_EXIT

    pyramids(nimages, nlev, r, c, I, W,
                  tmp_fullsize, tmp_halfsize, tmp_quartsize, tmp2_quartsize,
                  pyrW, pyrW_r, pyrW_c,
                  pyrI, pyrI_r, pyrI_c);
    blend(nimages, nlev,
               pyr, pyr_r, pyr_c,
               pyrW, pyrW_r, pyrW_c,
               pyrI, pyrI_r, pyrI_c);

    //reconstruct laplacian pyramid
    reconstruct_laplacian_pyramid(CHANNELS,nlev,tmp_fullsize,tmp2_fullsize,pyr,pyr_r,pyr_c,r,c,R);
    return R;
}

void weights(uint32_t nimages, uint32_t npixels, uint32_t r, uint32_t c, double contrast_parm, double sat_parm, double wexp_parm,
             double **I, double **W, double *tmp_weights, double *tmp2_weights){
    //for each image, calculate the weight maps
    for (int n = 0; n < nimages; n++){
        for(int i = 0; i < npixels; i++){
            W[n][i] = (double)1.0;  //TODO: optimize by removing
        }

        if(contrast_parm > 0){
            rgb2gray(I[n], npixels, tmp2_weights); //TODO optimize
            conv3x3_monochrome_replicate(tmp2_weights,r,c,tmp_weights);
            for(int i = 0; i < npixels; i++){
                tmp_weights[i] = pow(fabs(tmp_weights[i]),contrast_parm); COST_INC_POW(1); COST_INC_ABS(1);
            }
            for(int i = 0; i < npixels; i++){
                W[n][i] = W[n][i] * tmp_weights[i]; COST_INC_MUL(1);
            }
        }

        if(sat_parm > 0){
            //saturation is computed as the standard deviation of the color channels
            for(int i = 0; i < npixels; i++){
                double r = I[n][i*3];
                double g = I[n][i*3+1];
                double b = I[n][i*3+2];
                double mu = (r + g + b) / 3.0;
                COST_INC_ADD(2);
                COST_INC_DIV(1);
                double rmu = r-mu;
                double gmu = g-mu;
                double bmu = b-mu;
                COST_INC_ADD(3);
                tmp_weights[i] = sqrt((rmu*rmu + gmu*gmu + bmu*bmu)/3.0);
                COST_INC_SQRT(1);
                COST_INC_ADD(3);
                COST_INC_MUL(3);
                COST_INC_DIV(1);
            }
            for(int i = 0; i < npixels; i++){
                tmp_weights[i] = pow(fabs(tmp_weights[i]),sat_parm); COST_INC_POW(1); COST_INC_ABS(1);
            }
            for(int i = 0; i < npixels; i++){
                W[n][i] = W[n][i] * tmp_weights[i]; COST_INC_MUL(1);
            }
        }

        if(wexp_parm > 0){
            for(int i = 0; i < npixels; i++){
                double r = I[n][i*3];
                double g = I[n][i*3+1];
                double b = I[n][i*3+2];
                double rz = r-0.5;
                double gz = g-0.5;
                double bz = b-0.5;
                COST_INC_ADD(3);
                double rzrz = rz*rz;
                double gzgz = gz*gz;
                double bzbz = bz*bz;
                COST_INC_MUL(3);
                r = exp(-12.5*rzrz);
                g = exp(-12.5*gzgz);
                b = exp(-12.5*bzbz);
                COST_INC_MUL(3);
                COST_INC_EXP(3);
                tmp_weights[i] = r*g*b;
                COST_INC_MUL(2);
            }
            for(int i = 0; i < npixels; i++){
                tmp_weights[i] = pow(fabs(tmp_weights[i]),wexp_parm); COST_INC_POW(1); COST_INC_ABS(1);
            }
            for(int i = 0; i < npixels; i++){
                W[n][i] = W[n][i] * tmp_weights[i]; COST_INC_MUL(1);
            }
        }

        for(int i = 0; i < npixels; i++){
            W[n][i] = W[n][i] + 1.0E-12; COST_INC_ADD(1);
        }
    }

    //normalize weights: the total sum of weights for each pixel should be 1 across all N images
    //memcpy(dst,src,src_len*sizeof(double));
    for(int i = 0; i < npixels; i++){
        tmp_weights[i] = W[0][i];
    }
    for (int n = 1; n < nimages; n++){
        for(int i = 0; i < npixels; i++){
            tmp_weights[i] = tmp_weights[i] + W[n][i]; COST_INC_ADD(1);
        }
    }
    for (int n = 0; n < nimages; n++){
        for(int i = 0; i < npixels; i++){
            W[n][i] = W[n][i] / tmp_weights[i]; COST_INC_DIV(1); //beware of division by zero
        }
    }

}

void pyramids(uint32_t nimages, uint32_t nlev, uint32_t r, uint32_t c, double **I, double **W,
              double *tmp_fullsize, double *tmp_halfsize, double *tmp_quartsize, double *tmp2_quartsize,
              double ***pyrW, uint32_t **pyrW_r, uint32_t **pyrW_c,
              double ***pyrI, uint32_t **pyrI_r, uint32_t **pyrI_c){
    //multiresolution blending
    for (int n = 0; n < nimages; n++){
        //construct 1-channel gaussian pyramid from weights
        gaussian_pyramid(W[n],r,c,1,nlev,tmp_halfsize,pyrW[n],pyrW_r[n],pyrW_c[n]);
    }
    for (int n = 0; n < nimages; n++){
        //construct 3-channel laplacian pyramid from images
        laplacian_pyramid(I[n],r,c,CHANNELS,nlev,tmp_fullsize,tmp_halfsize,tmp_quartsize,tmp2_quartsize,pyrI[n],pyrI_r[n],pyrI_c[n]);
    }
}

void blend(uint32_t nimages, uint32_t nlev,
           double **pyr, uint32_t *pyr_r, uint32_t *pyr_c,
           double ***pyrW, uint32_t **pyrW_r, uint32_t **pyrW_c,
           double ***pyrI, uint32_t **pyrI_r, uint32_t **pyrI_c){
    //weighted blend
    if(0 < nimages){
        int n = 0;
        for(int v = 0; v < nlev; v++){
            for(int i = 0; i < pyrI_r[n][v]; i++){
                for(int j = 0; j < pyrI_c[n][v]; j++){
                    for(int k = 0; k < CHANNELS; k++){
                        pyr[v][(i*pyr_c[v]+j)*CHANNELS+k] = pyrW[n][v][i*pyrI_c[n][v]+j] * pyrI[n][v][(i*pyrI_c[n][v]+j)*CHANNELS+k];
                    }
                }
            }
        }
    }
    for (int n = 1; n < nimages; n++){
        for(int v = 0; v < nlev; v++){
            for(int i = 0; i < pyrI_r[n][v]; i++){
                for(int j = 0; j < pyrI_c[n][v]; j++){
                    for(int k = 0; k < CHANNELS; k++){
                        pyr[v][(i*pyr_c[v]+j)*CHANNELS+k] += pyrW[n][v][i*pyrI_c[n][v]+j] * pyrI[n][v][(i*pyrI_c[n][v]+j)*CHANNELS+k];
                    }
                }
            }
        }
    }
}

void reconstruct_laplacian_pyramid(uint32_t channels, uint32_t nlev, double *tmp_fullsize, double *tmp2_fullsize, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c, uint32_t r, uint32_t c, double *dst){
    if (nlev-2 >= 0){
        int v = nlev-2;
        upsample(pyr[v+1],pyr_r[v+1],pyr_c[v+1],channels,pyr_r[v],pyr_c[v],tmp_fullsize,tmp2_fullsize);
        for(int i = 0; i < pyr_r[v]*pyr_c[v]*channels; i++){
            dst[i] = pyr[v][i] + tmp2_fullsize[i]; COST_INC_ADD(1);
        }
    }
    for (int v = nlev-3; v >= 0; v--){
        upsample(dst,pyr_r[v+1],pyr_c[v+1],channels,pyr_r[v],pyr_c[v],tmp_fullsize,tmp2_fullsize);
        for(int i = 0; i < pyr_r[v]*pyr_c[v]*channels; i++){
            dst[i] = pyr[v][i] + tmp2_fullsize[i]; COST_INC_ADD(1);
        }
    }
}

void gaussian_pyramid(double *im, uint32_t r, uint32_t c, uint32_t channels, uint32_t nlev, double *tmp_halfsize, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c){
    pyr[0] = im;
    if(1 < nlev){
        int v = 1;
        downsample(pyr[0],pyr_r[v-1],pyr_c[v-1],channels,tmp_halfsize,pyr_r[v],pyr_c[v],pyr[v]);
    }
    for(int v = 2; v < nlev; v++){
        //downsample image and store into level
        downsample(pyr[v-1],pyr_r[v-1],pyr_c[v-1],channels,tmp_halfsize,pyr_r[v],pyr_c[v],pyr[v]);
    }
}

void laplacian_pyramid(double *im, uint32_t r, uint32_t c, uint32_t channels, uint32_t nlev, double *tmp_fullsize, double *tmp_halfsize, double *tmp_quartsize, double *tmp2_quartsize, double **pyr, uint32_t *pyr_r, uint32_t *pyr_c){
    uint32_t S_r = r;
    uint32_t S_c = c;
    uint32_t T_r = r;
    uint32_t T_c = c;

    double *tmp = NULL;

    if(0 < nlev-1){
        int v = 0;
        S_r = pyr_r[v+1];
        S_c = pyr_c[v+1];
        downsample(im,T_r,T_c,channels,tmp_halfsize,S_r,S_c,tmp2_quartsize);
        upsample(tmp2_quartsize,S_r,S_c,channels,pyr_r[v],pyr_c[v],tmp_halfsize,pyr[v]);
        for(int i = 0; i < T_r*T_c*channels; i++){
            pyr[v][i] = im[i] - pyr[v][i]; COST_INC_ADD(1);
        }
        T_r = S_r;
        T_c = S_c;
        double *tmp = tmp_quartsize;
        tmp_quartsize = tmp2_quartsize;
        tmp2_quartsize = tmp;
    }
    for(int v = 1; v < nlev-1; v++){
        S_r = pyr_r[v+1];
        S_c = pyr_c[v+1];
        downsample(tmp_quartsize,T_r,T_c,channels,tmp_halfsize,S_r,S_c,tmp2_quartsize);
        upsample(tmp2_quartsize,S_r,S_c,channels,pyr_r[v],pyr_c[v],tmp_halfsize,pyr[v]);
        for(int i = 0; i < T_r*T_c*channels; i++){
            pyr[v][i] = tmp_quartsize[i] - pyr[v][i]; COST_INC_ADD(1);
        }
        T_r = S_r;
        T_c = S_c;
        tmp = tmp_quartsize;
        tmp_quartsize = tmp2_quartsize;
        tmp2_quartsize = tmp;
    }
    //memcpy(dst,src,src_len*sizeof(double));
    for(int i = 0; i < T_r*T_c*channels; i++){
        pyr[nlev-1][i] = tmp_quartsize[i];
    }
}

/**
 * @brief convolution of a multi-channel image with a separable 5x5 filter and border mode "symmetric" combined with downsampling (1/4th of the pixels)
 */
void downsample(double *im, uint32_t r, uint32_t c, uint32_t channels, double *tmp_halfsize, uint32_t down_r, uint32_t down_c, double *dst){
    //r is height (vertical), c is width (horizontal)
    //horizontal filter
    int c2 = c/2+((c-2) % 2); //tmp_halfsize is only half the size
    for(int i = 0; i < r; i++){
        for(int j = 2; j < c-2; j+=2){ //every 2nd column
            for(int k = 0; k < channels; k++){
                tmp_halfsize[(i*c2+(j/2))*channels+k] =
                        im[((i  )*c+(j-2))*channels+k]*.0625 +
                        im[((i  )*c+(j-1))*channels+k]*.25 +
                        im[((i  )*c+(j  ))*channels+k]*.375 +
                        im[((i  )*c+(j+1))*channels+k]*.25 +
                        im[((i  )*c+(j+2))*channels+k]*.0625;
                COST_INC_ADD(4);
                COST_INC_MUL(5);
            }
        }
        //left edge
        int j = 0; // 1 0 [0 1 2 ... ]
        for(int k = 0; k < channels; k++){
            tmp_halfsize[(i*c2+(j/2))*channels+k] =
                    im[((i  )*c+(j+1))*channels+k]*.3125 +
                    im[((i  )*c+(j  ))*channels+k]*.625 +
                    im[((i  )*c+(j+2))*channels+k]*.0625;
            COST_INC_ADD(2);
            COST_INC_MUL(3);
        }
        //right edge
        if((c-2) % 2 == 0){
            j = c-2; // [ ... -2 -1 0 1] 1
            for(int k = 0; k < channels; k++){
                tmp_halfsize[(i*c2+(j/2))*channels+k] =
                        im[((i  )*c+(j-2))*channels+k]*.0625 +
                        im[((i  )*c+(j-1))*channels+k]*.25 +
                        im[((i  )*c+(j  ))*channels+k]*.375 +
                        im[((i  )*c+(j+1))*channels+k]*.3125;
                COST_INC_ADD(3);
                COST_INC_MUL(4);
            }
        }else{
            j = c-1; // [ ... -2 -1 0] 0 -1
            for(int k = 0; k < channels; k++){
                tmp_halfsize[(i*c2+(j/2))*channels+k] =
                        im[((i  )*c+(j-2))*channels+k]*.0625 +
                        im[((i  )*c+(j-1))*channels+k]*.3125 +
                        im[((i  )*c+(j  ))*channels+k]*.625;
                COST_INC_ADD(2);
                COST_INC_MUL(3);
            }
        }
    }
    //vertical filter
    c = c2;
    for(int j = 0; j < c; j++){ //every column in tmp_halfsize = every 2nd column in im
        for(int i = 2; i < r-2; i+=2){ //every 2nd row in tmp_halfsize
            int i2 = i/2+((i-2) % 2);
            for(int k = 0; k < channels; k++){
                dst[(i2*c+j)*channels+k] =
                        tmp_halfsize[((i-2)*c+(j  ))*channels+k]*.0625 +
                        tmp_halfsize[((i-1)*c+(j  ))*channels+k]*.25 +
                        tmp_halfsize[((i  )*c+(j  ))*channels+k]*.375 +
                        tmp_halfsize[((i+1)*c+(j  ))*channels+k]*.25 +
                        tmp_halfsize[((i+2)*c+(j  ))*channels+k]*.0625;
                COST_INC_ADD(4);
                COST_INC_MUL(5);
            }
        }
        //top edge
        int i = 0; // 1 0 [0 1 2 ... ]
        int i2 = i/2+((i-2) % 2);
        for(int k = 0; k < channels; k++){
            dst[(i2*c+j)*channels+k] =
                    tmp_halfsize[((i+1)*c+(j  ))*channels+k]*.3125 +
                    tmp_halfsize[((i  )*c+(j  ))*channels+k]*.625 +
                    tmp_halfsize[((i+2)*c+(j  ))*channels+k]*.0625;
            COST_INC_ADD(2);
            COST_INC_MUL(3);
        }
        //bottom edge
        if((r-2) % 2 == 0){
            i = r-2; // [ ... -2 -1 0 1] 1
            i2 = i/2+((i-2) % 2);
            for(int k = 0; k < channels; k++){
                dst[(i2*c+j)*channels+k] =
                        tmp_halfsize[((i-2)*c+(j  ))*channels+k]*.0625 +
                        tmp_halfsize[((i-1)*c+(j  ))*channels+k]*.25 +
                        tmp_halfsize[((i  )*c+(j  ))*channels+k]*.375 +
                        tmp_halfsize[((i+1)*c+(j  ))*channels+k]*.3125;
                COST_INC_ADD(3);
                COST_INC_MUL(4);
            }
        }else{
            i = r-1; // [ ... -2 -1 0] 0 -1
            i2 = i/2+((i-2) % 2);
            for(int k = 0; k < channels; k++){
                dst[(i2*c+j)*channels+k] =
                        tmp_halfsize[((i-2)*c+(j  ))*channels+k]*.0625 +
                        tmp_halfsize[((i-1)*c+(j  ))*channels+k]*.3125 +
                        tmp_halfsize[((i  )*c+(j  ))*channels+k]*.625;
                COST_INC_ADD(2);
                COST_INC_MUL(3);
            }
        }
    }
}

void upsample(double *im, uint32_t r, uint32_t c, uint32_t channels, uint32_t up_r, uint32_t up_c, double *tmp_fullsize, double *dst){
    // x
    for(int i = 0; i < r; i++){ //every 2nd line
        for(int j = 2; j < up_c-2; j++){
            for(int k = 0; k < channels; k++){
                tmp_fullsize[(i*up_c+j)*channels+k] =
                        0.25*im[((i  )*c+(j/2-1))*channels+k] +
                        1.5*im[((i )*c+(j/2  ))*channels+k] +
                        0.25*im[((i  )*c+(j/2+1))*channels+k];
                COST_INC_ADD(2);
                COST_INC_MUL(3);
            }
            j++;
            for(int k = 0; k < channels; k++){
                tmp_fullsize[(i*up_c+j)*channels+k] =
                        im[((i  )*c+(j/2))*channels+k] +
                        im[((i  )*c+(j/2+1))*channels+k];
                COST_INC_ADD(1);
            }
        }
        //left edge
        int j = 0; // 0 0 [0 1 2 ... ]
        for(int k = 0; k < channels; k++){
            tmp_fullsize[(i*up_c+j)*channels+k] =
                    1.75*im[((i  )*c+(j/2  ))*channels+k] +
                    0.25*im[((i  )*c+(j/2+1))*channels+k];
            COST_INC_ADD(1);
            COST_INC_MUL(2);
        }
        j = 1; // -1 [-1 0 1 2 ... ]
        for(int k = 0; k < channels; k++){
            tmp_fullsize[(i*up_c+j)*channels+k] =
                    im[((i  )*c+(j/2))*channels+k] +
                    im[((i  )*c+(j/2+1))*channels+k];
            COST_INC_ADD(1);
        }
        if(up_c % 2 == 0){
            //right edge
            j = up_c-2; // [ ... -2 -1 0 1] 0
            for(int k = 0; k < channels; k++){
                tmp_fullsize[(i*up_c+j)*channels+k] =
                        0.25*im[((i  )*c+(j/2-1))*channels+k] +
                        1.75*im[((i  )*c+(j/2  ))*channels+k];
                COST_INC_ADD(1);
                COST_INC_MUL(2);
            }
            j = up_c-1; // [ ... -2 -1 0] 0 0
            for(int k = 0; k < channels; k++){
                tmp_fullsize[(i*up_c+j)*channels+k] =
                        2.0*im[((i  )*c+(j/2))*channels+k];
                COST_INC_MUL(1);
            }
        } else {
            //right edge (remaining)
            j = up_c-1; // [ ... -2 -1 0] 0 0
            for(int k = 0; k < channels; k++){
                tmp_fullsize[(i*up_c+j)*channels+k] =
                        0.25*im[((i  )*c+(j/2-1))*channels+k] +
                        1.75*im[((i  )*c+(j/2  ))*channels+k];
                COST_INC_ADD(1);
                COST_INC_MUL(2);
            }
        }
    }

    // y
    for(int j = 0; j < up_c; j++){ //all columns
        for(int i = 1; i < r-1; i++){
            for(int k = 0; k < channels; k++){
                dst[(2*i*up_c+j)*channels+k] =
                        tmp_fullsize[((i-1)*up_c+(j  ))*channels+k]*.0625 +
                        tmp_fullsize[((i  )*up_c+(j  ))*channels+k]*.375 +
                        tmp_fullsize[((i+1)*up_c+(j  ))*channels+k]*.0625;
                COST_INC_ADD(2);
                COST_INC_MUL(3);
            }
            for(int k = 0; k < channels; k++){
                dst[((2*i+1)*up_c+j)*channels+k] =
                        tmp_fullsize[((i)*up_c+(j  ))*channels+k]*.25 +
                        tmp_fullsize[((i+1)*up_c+(j  ))*channels+k]*.25;
                COST_INC_ADD(1);
                COST_INC_MUL(2);
            }
        }
        //top edge
        int i = 0; // 0 0 [0 1 2 ... ]
        for(int k = 0; k < channels; k++){
            dst[(2*0*up_c+j)*channels+k] =
                    tmp_fullsize[((i  )*up_c+(j  ))*channels+k]*.4375 +
                    tmp_fullsize[((i+1)*up_c+(j  ))*channels+k]*.0625;
            COST_INC_ADD(1);
            COST_INC_MUL(2);
        }
        for(int k = 0; k < channels; k++){
            dst[((2*0+1)*up_c+j)*channels+k] =
                    tmp_fullsize[((i)*up_c+(j  ))*channels+k]*.25 +
                    tmp_fullsize[((i+1)*up_c+(j  ))*channels+k]*.25;
            COST_INC_ADD(1);
            COST_INC_MUL(2);
        }
        if(up_r % 2 == 0){
            //bottom edge
            i = r-1; // [ ... -2 -1 0 1] 0
            for(int k = 0; k < channels; k++){
                dst[((2*i)*up_c+j)*channels+k] =
                        tmp_fullsize[((i-1)*up_c+(j  ))*channels+k]*.0625 +
                        tmp_fullsize[((i  )*up_c+(j  ))*channels+k]*.4375;
                COST_INC_ADD(1);
                COST_INC_MUL(2);
            }
            // [ ... -2 -1 0] 0 0
            for(int k = 0; k < channels; k++){
                dst[((2*i+1)*up_c+j)*channels+k] =
                        tmp_fullsize[((i)*up_c+(j  ))*channels+k]*.5;
                COST_INC_MUL(1);
            }
        }else{
            //bottom edge
            i = r-1; // [ ... -2 -1 0] 0 0
            for(int k = 0; k < channels; k++){
                dst[((2*i)*up_c+j)*channels+k] =
                        tmp_fullsize[((i-1)*up_c+(j  ))*channels+k]*.0625 +
                        tmp_fullsize[((i  )*up_c+(j  ))*channels+k]*.4375;
                COST_INC_ADD(1);
                COST_INC_MUL(2);
            }
        }
    }
}

/**
 * @brief Implementation of the MATLAB rgb2gray function
 *
 * See: http://www.mathworks.com/help/images/ref/rgb2gray.html
 *
 * @param rgb Input image
 * @param npixels Size of image in pixels
 * @param gray (out) Output image
 */
void rgb2gray(double *im, size_t npixels, double* dst){
    for(int i = 0; i < npixels; i++){
        double r = im[i*3];
        double g = im[i*3+1];
        double b = im[i*3+2];
        dst[i] = 0.2989 * r + 0.5870 * g + 0.1140 * b; //retain luminance, discard hue and saturation
        COST_INC_ADD(2);
        COST_INC_MUL(3);
    }
}

/**
 * @brief convolution of a monochrome image with a 3x3 filter and border mode "replication"
 */
void conv3x3_monochrome_replicate(double* im, uint32_t r, uint32_t c, double* dst){
    for(int i = 1; i < r-1; i++){
        for(int j = 1; j < c-1; j++){
            dst[i*c+j] =
                    im[(i-1)*c+(j)] +
                    im[(i)  *c+(j-1)] + im[(i)  *c+(j)]*-4.0 + im[(i)  *c+(j+1)] +
                    im[(i+1)*c+(j)];
             COST_INC_ADD(4);
             COST_INC_MUL(1);
        }
    }
    //edges
    for(int i = 1; i < r-1; i++){
        int j = 0;
        dst[i*c+j] =
                im[(i-1)*c+(j)] +
                im[(i)  *c+(j)] + im[(i)  *c+(j)]*-4.0 + im[(i)  *c+(j+1)] +
                im[(i+1)*c+(j)];
        COST_INC_ADD(4);
        COST_INC_MUL(1);
        j = c-1;
        dst[i*c+j] =
                im[(i-1)*c+(j)] +
                im[(i)  *c+(j-1)] + im[(i)  *c+(j)]*-4.0 + im[(i)  *c+(j)] +
                im[(i+1)*c+(j)];
        COST_INC_ADD(4);
        COST_INC_MUL(1);
    }
    for(int j = 1; j < c-1; j++){
        int i = 0;
        dst[i*c+j] =
                im[(i)  *c+(j)] +
                im[(i)  *c+(j-1)] + im[(i)  *c+(j)]*-4.0 + im[(i)  *c+(j+1)] +
                im[(i+1)*c+(j)];
        COST_INC_ADD(4);
        COST_INC_MUL(1);
        i = r-1;
        dst[i*c+j] =
                im[(i-1)*c+(j)] +
                im[(i)  *c+(j-1)] + im[(i)  *c+(j)]*-4.0 + im[(i)  *c+(j+1)] +
                im[(i)  *c+(j)];
        COST_INC_ADD(4);
        COST_INC_MUL(1);
    }
    //corners
    //top left
    int i = 0;
    int j = 0;
    dst[i*c+j] =
            im[(i  )*c+(j)] +
            im[(i  )*c+(j)] + im[(i  )*c+(j)]*-4.0 + im[(i  )*c+(j+1)] +
            im[(i+1)*c+(j)];
    COST_INC_ADD(4);
    COST_INC_MUL(1);
    //top right
    i = 0;
    j = c-1;
    dst[i*c+j] =
            im[(i  )*c+(j)] +
            im[(i  )*c+(j-1)] + im[(i  )*c+(j)]*-4.0 + im[(i  )*c+(j  )] +
            im[(i+1)*c+(j)];
    COST_INC_ADD(4);
    COST_INC_MUL(1);
    //bottom left
    i = r-1;
    j = 0;
    dst[i*c+j] =
            im[(i-1)*c+(j)] +
            im[(i  )*c+(j  )] + im[(i  )*c+(j)]*-4.0 + im[(i  )*c+(j+1)] +
            im[(i  )*c+(j)];
    COST_INC_ADD(4);
    COST_INC_MUL(1);
    //bottom right
    i = r-1;
    j = c-1;
    dst[i*c+j] =
            im[(i-1)*c+(j)] +
            im[(i  )*c+(j-1)] + im[(i  )*c+(j)]*-4.0 + im[(i  )*c+(j  )] +
            im[(i  )*c+(j)];
    COST_INC_ADD(4);
    COST_INC_MUL(1);

}
