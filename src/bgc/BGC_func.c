/*****************************************************************************
 * Function	:   BGC model realted functions
 * Version	:   October, 2014
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "../pihm.h"               /* Data Model and Variable Declarations     */
#include "../spa/spa.h"
#include "bgc.h"
//#include "flux_pihm.h"
#define _DEBUG_

void BGC_read (char *filename, bgc_struct BGCM, Model_Data PIHM)
{
    int             i, j;
    double          t1, t2, t3, t4, r1;
    char            fn[100];
    char           *projectname;
    char           *token, *tempname;
    FILE           *epc_file;
    FILE           *bgc_file;
    FILE           *co2_file;
    FILE           *ndep_file;
    FILE           *SWC_file;
    FILE           *STC_file;
    char            co2_fn[100];
    char            ndep_fn[100];
    char            SWC_fn[100];
    char            STC_fn[100];
    int             ensemble_mode;
    char            cmdstr[MAXSTRING];
    char            optstr[MAXSTRING];
    time_t          rawtime;
    struct tm      *timeinfo;
    enum EPC_VEGTYPE veg_type;
    epconst_struct *epc;
    control_struct *ctrl;
    co2control_struct *co2;
    siteconst_struct *sitec;
    ramp_ndep_struct *ramp_ndep;
    cstate_struct   cs;
    cinit_struct    cinit;
    nstate_struct   ns;

    timeinfo = (struct tm *)malloc (sizeof (struct tm));

    /* Detect if model is running in ensemble mode */
    tempname = (char *)malloc ((strlen (filename) + 1) * sizeof (char));
    strcpy (tempname, filename);
    if (strstr (tempname, ".") != 0)
    {
        token = strtok (tempname, ".");
        projectname = (char *)malloc ((strlen (token) + 1) * sizeof (char));
        strcpy (projectname, token);
        ensemble_mode = 1;
    }
    else
    {
        projectname = (char *)malloc ((strlen (filename) + 1)
           * sizeof (char));
        strcpy (projectname, filename);
        ensemble_mode = 0;
    }
    free (tempname);

    /* Read epc files */

    BGCM->epclist.nvegtypes = NVEGTYPES;
    BGCM->epclist.epc = (epconst_struct *)malloc(BGCM->epclist.nvegtypes * sizeof (epconst_struct));

    if (ensemble_mode == 0)
        printf ("\n Read ecophysiological constant files\n");

    for (veg_type = 0; veg_type < BGCM->epclist.nvegtypes; veg_type ++)
    {
        switch (veg_type)
        {
            case EPC_C3GRASS:
                strcpy (fn, "input/epc/c3grass.epc");
                epc_file = fopen (fn, "r");
                break;
            case EPC_C4GRASS:
                strcpy (fn,"input/epc/c4grass.epc");
                epc_file = fopen (fn, "r");
                break;
            case EPC_DBF:
                strcpy (fn,"input/epc/dbf.epc");
                epc_file = fopen (fn, "r");
                break;
            case EPC_DNF:
                strcpy (fn, "input/epc/dnf.epc");
                epc_file = fopen (fn, "r");
                break;
            case EPC_EBF:
                strcpy (fn, "input/epc/ebf.epc");
                epc_file = fopen (fn, "r");
                break;
            case EPC_ENF:
                strcpy (fn, "input/epc/enf.epc");
                epc_file = fopen (fn, "r");
                break;
            case EPC_SHRUB:
                strcpy (fn, "input/epc/shrub.epc");
                epc_file = fopen (fn, "r");
                break;
        }
        if (epc_file == NULL)
        {
            printf ("\n  Fatal Error: epc files are in use or do not exist!\n");
            exit (1);
        }

        epc = &(BGCM->epclist.epc[veg_type]);

        /* Skip header file */
        fgets (cmdstr, MAXSTRING, epc_file);
        /* Read epc */
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%d", &epc->woody);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%d", &epc->evergreen);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%d", &epc->c3_flag);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->transfer_days);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->litfall_days);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->leaf_turnover);
	/* force leaf turnover fraction to 1.0 if deciduous */
	if (!epc->evergreen)
	{
		epc->leaf_turnover = 1.0;
	}
	epc->froot_turnover = epc->leaf_turnover;
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->livewood_turnover);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t1);
	epc->daily_mortality_turnover = t1 / 365.0;
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t1);
	epc->daily_fire_turnover = t1/365;
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->alloc_frootc_leafc);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->alloc_newstemc_newleafc);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->alloc_newlivewoodc_newwoodc);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->alloc_crootc_stemc);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->alloc_prop_curgrowth);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->leaf_cn);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->leaflitr_cn);
	/* test for leaflitter C:N > leaf C:N */
	if (epc->leaflitr_cn < epc->leaf_cn)
	{
		printf("Error: leaf litter C:N must be >= leaf C:N\n");
		printf("change the values in ECOPHYS block of initialization file %s\n", fn);
		exit(1);
	}
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->froot_cn);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->livewood_cn);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->deadwood_cn);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t1);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t2);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t3);
	epc->leaflitr_flig = t3;
	/* test for litter fractions sum to 1.0 */
	if (fabs(t1 + t2 + t3 - 1.0) > FLT_COND_TOL)
	{
		printf("Error:\n");
		printf("leaf litter proportions of labile, cellulose, and lignin\n");
		printf("must sum to 1.0. Check initialization file and try again %s.\n", fn);
                exit(1);
	}
	/* calculate shielded and unshielded cellulose fraction */
        r1 = t3 / t2;
        if (r1 <= 0.45)
        {
                epc->leaflitr_fscel = 0.0;
                epc->leaflitr_fucel = t2;
        }
        else if (r1 > 0.45 && r1 < 0.7)
        {
                t4 = (r1 - 0.45) * 3.2;
                epc->leaflitr_fscel = t4 * t2;
                epc->leaflitr_fucel = (1.0 - t4) * t2;
        }
        else
        {
                epc->leaflitr_fscel = 0.8 * t2;
                epc->leaflitr_fucel = 0.2 * t2;
        }
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t1);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t2);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t3);
        epc->frootlitr_flig = t3;
	if (fabs(t1 + t2 + t3 - 1.0) > FLT_COND_TOL)
	{
		printf("Error:\n");
		printf("froot litter proportions of labile, cellulose, and lignin\n");
		printf("must sum to 1.0. Check initialization file and try again.\n");
                exit(1);
	}
	/* calculate shielded and unshielded cellulose fraction */
        r1 = t3 / t2;
        if (r1 <= 0.45)
        {
                epc->frootlitr_fscel = 0.0;
                epc->frootlitr_fucel = t2;
        }
        else if (r1 > 0.45 && r1 < 0.7)
        {
                t4 = (r1 - 0.45) * 3.2;
                epc->frootlitr_fscel = t4 * t2;
                epc->frootlitr_fucel = (1.0 - t4) * t2;
        }
        else
        {
                epc->frootlitr_fscel = 0.8 * t2;
                epc->frootlitr_fucel = 0.2 * t2;
        }
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t1);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &t2);
	epc->deadwood_flig = t2;
	/* test for litter fractions sum to 1.0 */
	if (fabs( t1 + t2 - 1.0) > FLT_COND_TOL)
	{
		printf("Error:\n");
		printf("deadwood proportions of cellulose and lignin must sum\n");
		printf("to 1.0. Check initialization file and try again.\n");
                exit(1);
	}
	/* calculate shielded and unshielded cellulose fraction */
        r1 = t2/t1;
        if (r1 <= 0.45)
        {
            epc->deadwood_fscel = 0.0;
            epc->deadwood_fucel = t1;
        }
        else if (r1 > 0.45 && r1 < 0.7)
        {
            t4 = (r1 - 0.45) * 3.2;
            epc->deadwood_fscel = t4 * t1;
            epc->deadwood_fucel = (1.0 - t4) * t1;
        }
        else
        {
            epc->deadwood_fscel = 0.8 * t1;
            epc->deadwood_fucel = 0.2 * t1;
        }
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->int_coef);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->ext_coef);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->lai_ratio);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->avg_proj_sla);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->sla_ratio);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->flnr);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->gl_smax);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->gl_c);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->gl_bl);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->psi_open);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->psi_close);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->vpd_open);
        fgets (cmdstr, MAXSTRING, epc_file);
        sscanf (cmdstr, "%lf", &epc->vpd_close);

//#ifdef _DEBUG_
//        printf ("WOODY%d\t", epc->woody);
//        printf ("EVERGREEN%d\t", epc->evergreen);
//        printf ("C3%d\t", epc->c3_flag);
//        printf ("TRANSFERDAY%lf\t", epc->transfer_days);
//        printf ("LITFALLDAY%lf\t", epc->litfall_days);
//        printf ("LEAFTURNOVER%lf\t", epc->leaf_turnover);
//	printf ("FROOTTURNOVER%lf\t", epc->froot_turnover);
//        printf ("LIVEWOODTURNOVER%lf\t", epc->livewood_turnover);
//	printf ("DAILYMORTALITYTURNOVER%lf\t", epc->daily_mortality_turnover);
//	printf ("DAILYFIRETURNOVER%lf\t", epc->daily_fire_turnover);
//        printf ("FROOTC/LEAFC%lf\t", epc->alloc_frootc_leafc);
//        printf ("NEWSTEMC/NEWLEAFC%lf\t", epc->alloc_newstemc_newleafc);
//        printf ("NEWLIVEWOODC/NEWWOODC%lf\t", epc->alloc_newlivewoodc_newwoodc);
//        printf ("CROOTC/STEMC%lf\t", epc->alloc_crootc_stemc);
//        printf ("PROP%lf\t", epc->alloc_prop_curgrowth);
//        printf ("LEAFCN%lf\t", epc->leaf_cn);
//        printf ("LEAFLITRCN%lf\t", epc->leaflitr_cn);
//        printf ("FROOTCN%lf\t", epc->froot_cn);
//        printf ("LIVEWOODCN%lf\t", epc->livewood_cn);
//        printf ("DEADWOODCN%lf\t", epc->deadwood_cn);
//        printf ("LEAFLITR %lf %lf %lf\t", epc->leaflitr_fscel, epc->leaflitr_fucel, epc->leaflitr_flig);
//        printf ("FROOTLITR %lf %lf %lf\t", epc->frootlitr_fscel, epc->frootlitr_fucel, epc->frootlitr_flig);
//        printf ("DEADWOOD %lf %lf %lf\t", epc->deadwood_fscel, epc->deadwood_fucel, epc->deadwood_flig);
//        printf ("INTCOEF %lf\t", epc->int_coef);
//        printf ("EXTCOEF %lf\t", epc->ext_coef);
//        printf ("LAIRATIO %lf\t", epc->lai_ratio);
//        printf ("PROJSLA %lf\t", epc->avg_proj_sla);
//        printf ("SLARATIO %lf\t", epc->sla_ratio);
//        printf ("FLNR %lf\t", epc->flnr);
//        printf ("GLMAX %lf\t", epc->gl_smax);
//        printf ("GLC %lf\t", epc->gl_c);
//        printf ("GL_BL %lf\t", epc->gl_bl);
//        printf ("PSIOPEN %lf\t", epc->psi_open);
//        printf ("PSICLOSE %lf\t", epc->psi_close);
//        printf ("VPDOPEN %lf\t", epc->vpd_open);
//        printf ("VPDCLOSE%lf\n", epc->vpd_close);
//#endif
    }

    /* Read bgc simulation control file */

    ctrl = &BGCM->ctrl;
    co2 = &BGCM->co2;
    sitec = &BGCM->sitec;
    ramp_ndep = &BGCM->ramp_ndep;
    
    if (ensemble_mode == 0)
        printf (" Read %s.bgc ...", projectname);
    sprintf (fn, "input/%s.bgc", projectname);
    bgc_file = fopen (fn, "r");
    
    if (bgc_file == NULL)
    {
        printf ("\n  Fatal Error: %s.bgc is in use or does not exist!\n", projectname);
        exit (1);
    }

    fgets (cmdstr, MAXSTRING, bgc_file);

    while (!feof (bgc_file))
    {
        if (cmdstr[0] != '#' && cmdstr[0] != '\n' && cmdstr[0] != '\0')
        {
            sscanf (cmdstr, "%s", optstr);

            if (strcasecmp ("TIME_DEFINE", optstr) == 0)
            {
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%d", &ctrl->spinupstart);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%d", &ctrl->spinupend);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%d", &ctrl->spinup);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%d", &ctrl->maxspinyears);
            }
            else if (strcasecmp ("CO2_CONTROL", optstr) == 0)
            {
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%d", &co2->varco2);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &co2->co2ppm);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%s", &co2_fn);
            }
            else if (strcasecmp ("SITE", optstr) == 0)
            {
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &sitec->ndep);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &sitec->nfix);
            }
            else if (strcasecmp ("RAMP_NDEP", optstr) == 0)
            {
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%d", &ramp_ndep->doramp);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%d", &ramp_ndep->ind_year);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &ramp_ndep->ind_ndep);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%s", &ndep_fn);
            }
            else if (strcasecmp ("C_STATE", optstr) ==0)
            {
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cinit.max_leafc);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cinit.max_stemc);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.cwdc);
                ns.cwdn = cs.cwdc / epc->deadwood_cn;
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.litr1c);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.litr2c);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.litr3c);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.litr4c);
                ns.litr2n = cs.litr2c / epc->leaflitr_cn;
                ns.litr3n = cs.litr3c / epc->leaflitr_cn;
                ns.litr4n = cs.litr4c / epc->leaflitr_cn;
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.soil1c);
                ns.soil1n = cs.soil1c / SOIL1_CN;
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.soil2c);
                ns.soil2n = cs.soil2c / SOIL2_CN;
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.soil3c);
                ns.soil3n = cs.soil3c / SOIL3_CN;
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &cs.soil4c);
                ns.soil4n = cs.soil4c / SOIL4_CN;
                fgets (cmdstr, MAXSTRING, bgc_file);
            }
            else if (strcasecmp ("N_STATE", optstr) ==0)
            {
                sscanf (cmdstr, "%lf", &ns.litr1n);
                fgets (cmdstr, MAXSTRING, bgc_file);
                sscanf (cmdstr, "%lf", &ns.sminn);
            }
            fgets (cmdstr, MAXSTRING, bgc_file);
        }
        fgets (cmdstr, MAXSTRING, bgc_file);
    }
    
    fclose(bgc_file);

    BGCM->Forcing = (TSD **) malloc (4 * sizeof (TSD *));

    /* Read CO2 and Ndep files */
    if (co2->varco2 == 1)
    {
        BGCM->Forcing[CO2_TS] = (TSD *) malloc (sizeof (TSD));
        co2_file = fopen(co2_fn, "r");
        if (co2_file == NULL)
        {
            printf ("\n  Fatal Error: co2 file %s in use or do not exist!\n", co2_fn);
            exit (1);
        }
        BGCM->Forcing[CO2_TS][0].length = 0;
        /* Count lines */
        fgets (cmdstr, MAXSTRING, co2_file);
        while (!feof (co2_file))
        {
            if (cmdstr[0] != '\n' && cmdstr[0] != '\0' && cmdstr[0] != '\t')
                BGCM->Forcing[CO2_TS][0].length = BGCM->Forcing[CO2_TS][0].length + 1;
            fgets (cmdstr, MAXSTRING, co2_file);
        }
        printf ("Number of lines = %d", BGCM->Forcing[CO2_TS][0].length);
        BGCM->Forcing[CO2_TS][0].TS = (double **) malloc ((BGCM->Forcing[CO2_TS][0].length) * sizeof (double *));
        for (i = 0; i < BGCM->Forcing[CO2_TS][0].length; i++)
        {
            BGCM->Forcing[CO2_TS][0].TS[i] = (double *) malloc (2 * sizeof (double));
            fscanf (co2_file, "%d", &timeinfo->tm_year, &BGCM->Forcing[CO2_TS][0].TS[i][1]);
            timeinfo->tm_year = timeinfo->tm_year - 1900;
            timeinfo->tm_mon = 0;
            timeinfo->tm_mday = 1;
            timeinfo->tm_hour = 0;
            timeinfo->tm_min = 0;
            timeinfo->tm_sec = 0;
            BGCM->Forcing[CO2_TS][0].TS[i][0] = (double) rawtime;
        }

        fclose (co2_file);
    }
    if (ramp_ndep->doramp == 2)
    {
        BGCM->Forcing[NDEP_TS] = (TSD *) malloc (sizeof (TSD));
        ndep_file = fopen(ndep_fn, "r");
        if (ndep_file == NULL)
        {
            printf ("\n  Fatal Error: N deposition file %s is in use or do not exist!\n", ndep_fn);
            exit (1);
        }
        BGCM->Forcing[NDEP_TS][0].length = 0;
        /* Count lines */
        fgets (cmdstr, MAXSTRING, ndep_file);
        while (!feof (ndep_file))
        {
            if (cmdstr[0] != '\n' && cmdstr[0] != '\0' && cmdstr[0] != '\t')
                BGCM->Forcing[NDEP_TS][0].length = BGCM->Forcing[NDEP_TS][0].length + 1;
            fgets (cmdstr, MAXSTRING, ndep_file);
        }
        printf ("Number of lines = %d\n", BGCM->Forcing[NDEP_TS][0].length);
        BGCM->Forcing[NDEP_TS][0].TS = (double **) malloc ((BGCM->Forcing[NDEP_TS][0].length) * sizeof (double *));
        for (i = 0; i < BGCM->Forcing[NDEP_TS][0].length; i++)
        {
            BGCM->Forcing[NDEP_TS][0].TS[i] = (double *) malloc (2 * sizeof (double));
            fscanf (ndep_file, "%d", &timeinfo->tm_year, &BGCM->Forcing[NDEP_TS][0].TS[i][1]);
            timeinfo->tm_year = timeinfo->tm_year - 1900;
            timeinfo->tm_mon = 0;
            timeinfo->tm_mday = 1;
            timeinfo->tm_hour = 0;
            timeinfo->tm_min = 0;
            timeinfo->tm_sec = 0;
            BGCM->Forcing[NDEP_TS][0].TS[i][0] = (double) rawtime;
        }
        fclose (ndep_file);
    }
    /* Read soil moisture and soil temperature "forcing" */
    if (ctrl->spinup == 1)
    {
        /* Read soil moisture forcing */
        BGCM->Forcing[SWC_TS] = (TSD *)malloc (PIHM->NumEle * sizeof (TSD));
        sprintf (SWC_fn, "input/%s.SWC", filename);
        SWC_file = fopen(SWC_fn, "rb");
        if (SWC_file == NULL)
        {
            printf ("\n  Fatal Error: soil water content file %s is in use or do not exist!\n", SWC_fn);
            exit (1);
        }
        /* Check the length of forcing */
        fseek (SWC_file, 0L, SEEK_END);
        for (i = 0; i < PIHM->NumEle; i++)
        {
            BGCM->Forcing[SWC_TS][i].length = (int)(ftell(SWC_file) / (PIHM->NumEle + 1) / 8);  /* 8 is the size of double */
            BGCM->Forcing[SWC_TS][i].TS = (double **) malloc (BGCM->Forcing[SWC_TS][i].length * sizeof (double *));
        }
        /* Read in forcing */
        rewind (SWC_file);
        for (j = 0; j < BGCM->Forcing[SWC_TS][0].length; j++)
        {
            for (i = 0; i < PIHM->NumEle; i++)
                BGCM->Forcing[SWC_TS][i].TS[j] = (double *) malloc (2 * sizeof (double));
            fread (&BGCM->Forcing[SWC_TS][0].TS[j][0], sizeof (double), 1, SWC_file);
#ifdef _DEBUG_
            rawtime = (int)BGCM->Forcing[SWC_TS][0].TS[j][0];
            timeinfo = gmtime (&rawtime);
            printf ("%4.4d-%2.2d-%2.2d %2.2d:%2.2d\t", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min);
#endif
            for (i = 0; i < PIHM->NumEle; i++)
            {
                BGCM->Forcing[SWC_TS][i].TS[j][0] = BGCM->Forcing[SWC_TS][0].TS[j][0];
                fread (&BGCM->Forcing[SWC_TS][i].TS[j][1], sizeof (double), 1, SWC_file);
#ifdef _DEBUG_
                printf("%lf\t", BGCM->Forcing[SWC_TS][i].TS[j][1]);
#endif
            }
#ifdef _DEBUG_
            printf("\n");
#endif
        }
        fclose (SWC_file);

        /* Read soil temperature forcing */
        BGCM->Forcing[STC_TS] = (TSD *)malloc (PIHM->NumEle * sizeof (TSD));
        sprintf (STC_fn, "input/%s.STC", filename);
        STC_file = fopen(STC_fn, "rb");
        if (STC_file == NULL)
        {
            printf ("\n  Fatal Error: soil temperature file %s is in use or do not exist!\n", STC_fn);
            exit (1);
        }
        /* Check the length of forcing */
        fseek (STC_file, 0L, SEEK_END);
        for (i = 0; i < PIHM->NumEle; i++)
        {
            BGCM->Forcing[STC_TS][i].length = (int)(ftell(STC_file) / (PIHM->NumEle + 1) / 8);  /* 8 is the size of double */
            BGCM->Forcing[STC_TS][i].TS = (double **) malloc (BGCM->Forcing[STC_TS][i].length * sizeof (double *));
        }
        /* Read in forcing */
        rewind (STC_file);
        for (j = 0; j < BGCM->Forcing[STC_TS][0].length; j++)
        {
            for (i = 0; i < PIHM->NumEle; i++)
                BGCM->Forcing[STC_TS][i].TS[j] = (double *) malloc (2 * sizeof (double));
            fread (&BGCM->Forcing[STC_TS][0].TS[j][0], sizeof (double), 1, STC_file);
#ifdef _DEBUG_
            rawtime = (int)BGCM->Forcing[STC_TS][0].TS[j][0];
            timeinfo = gmtime (&rawtime);
            printf ("%4.4d-%2.2d-%2.2d %2.2d:%2.2d\t", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min);
#endif
            for (i = 0; i < PIHM->NumEle; i++)
            {
                BGCM->Forcing[STC_TS][i].TS[j][0] = BGCM->Forcing[STC_TS][0].TS[j][0];
                fread (&BGCM->Forcing[STC_TS][i].TS[j][1], sizeof (double), 1, STC_file);
#ifdef _DEBUG_
                printf("%lf\t", BGCM->Forcing[STC_TS][i].TS[j][1]);
#endif
            }
#ifdef _DEBUG_
            printf("\n");
#endif
        }
        fclose (STC_file);
    }

    free (projectname);
    if (ensemble_mode == 0)
        printf ("done.\n");
}

void BGC_presim_state_init (bgc_struct BGCM, Model_Data PIHM)
{
    /*
     * Program to initialize BGC state variables
     * Adapted from presim_state_init in biome-bgc
     */
    int i;

    BGCM->grid = (bgc_grid *) malloc (PIHM->NumEle * sizeof (bgc_grid));

    wstate_struct *ws;
    cstate_struct *cs;
    nstate_struct *ns;
    cinit_struct *cinit;

    for (i = 0; i < PIHM->NumEle; i++)
    {
        ws = &BGCM->grid[i].ws;
        cs = &BGCM->grid[i].cs;
        ns = &BGCM->grid[i].ns;
        cinit = &BGCM->grid[i].cinit;

        ws->soilw = 0.0;
        ws->snoww = 0.0;
        ws->canopyw = 0.0;
        ws->prcp_src = 0.0;
        ws->outflow_snk = 0.0;
        ws->soilevap_snk = 0.0;
        ws->snowsubl_snk = 0.0;
        ws->canopyevap_snk = 0.0;
        ws->trans_snk = 0.0;

        cinit->max_leafc = 0.0;
        cinit->max_stemc = 0.0;

        cs->leafc = 0.0;
        cs->leafc_storage = 0.0;
        cs->leafc_transfer = 0.0;
        cs->frootc = 0.0;
        cs->frootc_storage = 0.0;
        cs->frootc_transfer = 0.0;
        cs->livestemc = 0.0;
        cs->livestemc_storage = 0.0;
        cs->livestemc_transfer = 0.0;
        cs->deadstemc = 0.0;
        cs->deadstemc_storage = 0.0;
        cs->deadstemc_transfer = 0.0;
        cs->livecrootc = 0.0;
        cs->livecrootc_storage = 0.0;
        cs->livecrootc_transfer = 0.0;
        cs->deadcrootc = 0.0;
        cs->deadcrootc_storage = 0.0;
        cs->deadcrootc_transfer = 0.0;
        cs->gresp_storage = 0.0;
        cs->gresp_transfer = 0.0;
        cs->cwdc = 0.0;
        cs->litr1c = 0.0;
        cs->litr2c = 0.0;
        cs->litr3c = 0.0;
        cs->litr4c = 0.0;
        cs->soil1c = 0.0;
        cs->soil2c = 0.0;
        cs->soil3c = 0.0;
        cs->soil4c = 0.0;
        cs->cpool = 0.0;
        cs->psnsun_src = 0.0;
        cs->psnshade_src = 0.0;
        cs->leaf_mr_snk = 0.0;
        cs->leaf_gr_snk = 0.0;
        cs->froot_mr_snk = 0.0;
        cs->froot_gr_snk = 0.0;
        cs->livestem_mr_snk = 0.0;
        cs->livestem_gr_snk = 0.0;
        cs->deadstem_gr_snk = 0.0;
        cs->livecroot_mr_snk = 0.0;
        cs->livecroot_gr_snk = 0.0;
        cs->deadcroot_gr_snk = 0.0;
        cs->litr1_hr_snk = 0.0;
        cs->litr2_hr_snk = 0.0;
        cs->litr4_hr_snk = 0.0;
        cs->soil1_hr_snk = 0.0;
        cs->soil2_hr_snk = 0.0;
        cs->soil3_hr_snk = 0.0;
        cs->soil4_hr_snk = 0.0;
        cs->fire_snk = 0.0;

        ns->leafn = 0.0;
        ns->leafn_storage = 0.0;
        ns->leafn_transfer = 0.0;
        ns->frootn = 0.0;
        ns->frootn_storage = 0.0;
        ns->frootn_transfer = 0.0;
        ns->livestemn = 0.0;
        ns->livestemn_storage = 0.0;
        ns->livestemn_transfer = 0.0;
        ns->deadstemn = 0.0;
        ns->deadstemn_storage = 0.0;
        ns->deadstemn_transfer = 0.0;
        ns->livecrootn = 0.0;
        ns->livecrootn_storage = 0.0;
        ns->livecrootn_transfer = 0.0;
        ns->deadcrootn = 0.0;
        ns->deadcrootn_storage = 0.0;
        ns->deadcrootn_transfer = 0.0;
        ns->cwdn = 0.0;
        ns->litr1n = 0.0;
        ns->litr2n = 0.0;
        ns->litr3n = 0.0;
        ns->litr4n = 0.0;
        ns->soil1n = 0.0;
        ns->soil2n = 0.0;
        ns->soil3n = 0.0;
        ns->soil4n = 0.0;
        ns->sminn = 0.0;
        ns->retransn = 0.0;
        ns->npool = 0.0;
        ns->nfix_src = 0.0;
        ns->ndep_src = 0.0;
        ns->nleached_snk = 0.0;
        ns->nvol_snk = 0.0;
        ns->fire_snk = 0.0;
    }
}
