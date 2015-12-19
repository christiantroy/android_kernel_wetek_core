#ifndef _DNR_H
#define _DNR_H

#define Wr(adr, val) WRITE_VCBUS_REG(adr, val)
#define Rd(adr) READ_VCBUS_REG(adr)
#define Wr_reg_bits(adr, val, start, len) WRITE_VCBUS_REG_BITS(adr, val, start, len)

typedef struct dnr_param_s{
    char *name;
    int *addr;
}dnr_param_t;

typedef struct {
    int prm_sw_gbs_ctrl;
    int prm_gbs_vldcntthd;
    int prm_gbs_cnt_min;
    int prm_gbs_ratcalcmod;
    int prm_gbs_ratthd[3];
    int prm_gbs_difthd[3];
    int prm_gbs_bsdifthd;
    int prm_gbs_calcmod;
    int sw_gbs;
    int sw_gbs_vld_flg;
    int sw_gbs_vld_cnt;
    int prm_hbof_minthd;
    int prm_hbof_ratthd0;
    int prm_hbof_ratthd1;
    int prm_hbof_vldcntthd;
    int sw_hbof;
    int sw_hbof_vld_flg;
    int sw_hbof_vld_cnt;
    int prm_vbof_minthd;
    int prm_vbof_ratthd0;
    int prm_vbof_ratthd1;
    int prm_vbof_vldcntthd;
    int sw_vbof;
    int sw_vbof_vld_flg;
    int sw_vbof_vld_cnt;
}DNR_PRM_t;//used for software
// software parameters initialization�� initializing before used
void dnr_init(struct device *dev);

int global_bs_calc_sw(int *pGbsVldCnt,
                      int *pGbsVldFlg,
                      int *pGbs,
                      int nGbsStatLR,
                      int nGbsStatLL,
                      int nGbsStatRR,
                      int nGbsStatDif,
                      int nGbsStatCnt,
                      int prm_gbs_vldcntthd, //prm below
                      int prm_gbs_cnt_min,
                      int prm_gbs_ratcalcmod,
                      int prm_gbs_ratthd[3],
                      int prm_gbs_difthd[3],
                      int prm_gbs_bsdifthd,
                      int prm_gbs_calcmod);

int hor_blk_ofst_calc_sw(int *pHbOfVldCnt,
                         int *pHbOfVldFlg,
                         int *pHbOfst,
                         int nHbOfStatCnt[32],
			 int nXst,
			 int nXed,
                         int prm_hbof_minthd,
                         int prm_hbof_ratthd0,
			 int prm_hbof_ratthd1,
                         int prm_hbof_vldcntthd,
			 int nRow,
			 int nCol);

int hor_blk_ofst_calc_sw(int *pHbOfVldCnt,
                         int *pHbOfVldFlg,
                         int *pHbOfst,
                         int nHbOfStatCnt[32],
			 int nXst,
			 int nXed,
                         int prm_hbof_minthd,
                         int prm_hbof_ratthd0,
			 int prm_hbof_ratthd1,
			 int prm_hbof_vldcntthd,
			 int nRow,
			 int nCol);

int ver_blk_ofst_calc_sw(int *pVbOfVldCnt,
                         int *pVbOfVldFlg,
                         int *pVbOfst,
                         int nVbOfStatCnt[32],
			 int nYst,
			 int nYed,
                         int prm_vbof_minthd,
                         int prm_vbof_ratthd0,
			 int prm_vbof_ratthd1,
                         int prm_vbof_vldcntthd,
			 int nRow,
			 int nCol);
void run_dnr_in_irq(int nCol,int nRow);
#endif

