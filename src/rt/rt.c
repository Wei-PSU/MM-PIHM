/*******************************************************************************
* RT-Flux-PIHM is a finite volume based, reactive transport module that operates
* on top of the hydrological land surface processes described by Flux-PIHM.
* RT-Flux-PIHM tracks the transportation and reaction in a given watershed. It
* uses operator splitting technique to couple transport and reaction.
*****************************************************************************/
#include "pihm.h"

/* Begin global variable definition (MACRO) */
#define UNIT_C 1440
#define ZERO   1E-20
#define LINE_WIDTH 512
#define WORDS_LINE 40
#define WORD_WIDTH 80
#define INFTYSMALL  1E-6
#define MIN(a,b) (((a)<(b))? (a):(b))
#define MAX(a,b) (((a)>(b))? (a):(b))

void Monitor(realtype stepsize, const pihm_struct pihm, Chem_Data CD)
{
    int             i;
    double          unit_c = stepsize / UNIT_C;

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (i = 0; i < nelem; i++)
    {
        double          resflux = 0.0;
        double          sumflux1, sumflux2;
        int             j;

        /*
         * Correct recharge in the saturated zone
         */
        for (j = 0; j < NUM_EDGE; j++)
        {
            resflux -= CD->Flux[RT_LAT_GW(i, j)].flux * unit_c;
        }

        sumflux1 =
            (CD->Vcele[RT_GW(i)].height_t - CD->Vcele[RT_GW(i)].height_o) *
            pihm->elem[i].topo.area * CD->Vcele[RT_GW(i)].porosity;
        sumflux2 = sumflux1 - resflux;
        /* Flux: in negative, out positive */
        CD->Flux[RT_RECHG_GW(i)].flux = -sumflux2 * UNIT_C / stepsize;
        CD->Flux[RT_RECHG_UNSAT(i)].flux = -CD->Flux[RT_RECHG_GW(i)].flux;

        /*
         * Correct infiltration in the unsaturated zone
         */
        sumflux1 =
            (CD->Vcele[RT_UNSAT(i)].height_t - CD->Vcele[RT_UNSAT(i)].height_o) *
            pihm->elem[i].topo.area * CD->Vcele[RT_UNSAT(i)].porosity;
        sumflux2 = sumflux1 + CD->Flux[RT_RECHG_UNSAT(i)].flux * unit_c;
        CD->Flux[RT_INFIL(i)].flux = -sumflux2 * UNIT_C / stepsize;
        /* Input of rain water chemistry can not be negative, i.e., infil.flux
         * should be negative */
        CD->Flux[RT_INFIL(i)].flux = MIN(CD->Flux[RT_INFIL(i)].flux, 0.0);
        /* In addition, the soil evaporation leaves chemicals inside */
        CD->Flux[RT_INFIL(i)].flux -=
            fabs(pihm->elem[i].wf.edir_unsat + pihm->elem[i].wf.edir_gw) *
            86400 * pihm->elem[i].topo.area;
    }
}

int upstream(elem_struct up, elem_struct lo, const pihm_struct pihm)
{
    /* Locate the upstream grid of up -> lo flow */
    /* Require verification                      */
    /* only determines points in triangular elements */
    double          x_, y_;
    int             i;

    x_ = 2 * up.topo.x - lo.topo.x;
    y_ = 2 * up.topo.y - lo.topo.y;

    for (i = 0; i < nelem; i++)
    {
        double          x_a, x_b, x_c;
        double          y_a, y_b, y_c;
        double          dot00, dot01, dot02, dot11, dot12, u, v, invDenom;

        /* Find point lies in which triangular element, a very interesting
         * method */
        if ((i != (up.ind - 1)) && (i != (lo.ind - 1)))
        {
            x_a = pihm->meshtbl.x[pihm->elem[i].node[0] - 1];
            x_b = pihm->meshtbl.x[pihm->elem[i].node[1] - 1];
            x_c = pihm->meshtbl.x[pihm->elem[i].node[2] - 1];
            y_a = pihm->meshtbl.y[pihm->elem[i].node[0] - 1];
            y_b = pihm->meshtbl.y[pihm->elem[i].node[1] - 1];
            y_c = pihm->meshtbl.y[pihm->elem[i].node[2] - 1];
            dot00 = (x_c - x_a) * (x_c - x_a) + (y_c - y_a) * (y_c - y_a);
            dot01 = (x_c - x_a) * (x_b - x_a) + (y_c - y_a) * (y_b - y_a);
            dot02 = (x_c - x_a) * (x_ - x_a) + (y_c - y_a) * (y_ - y_a);
            dot11 = (x_b - x_a) * (x_b - x_a) + (y_b - y_a) * (y_b - y_a);
            dot12 = (x_b - x_a) * (x_ - x_a) + (y_b - y_a) * (y_ - y_a);
            invDenom = 1.0 / (dot00 * dot11 - dot01 * dot01);
            u = (dot11 * dot02 - dot01 * dot12) * invDenom;
            v = (dot00 * dot12 - dot01 * dot02) * invDenom;
            if ((u > 0.0) && (v > 0.0) && (u + v < 1.0))
            {
                return pihm->elem[i].ind;
            }
        }
    }

    return 0;
}

int realcheck(const char *words)
{
    int             flg = 1, i;
    if (((words[0] >= '0') && (words[0] <= '9')) ||
        (words[0] == '.') || (words[0] == '-') || (words[0] == '+'))
    {
        for (i = 0; i < (int)strlen(words); i++)
        {
            /* Ascii 10 is new line and 13 is carriage return */
            if ((words[i] > '9' || words[i] < '+') && (words[i] != 'E')
                && (words[i] != 'e') && (words[i] != 10) && (words[i] != 13))
            {
                flg = 0;
            }
        }
    }
    else
    {
        flg = 0;
    }
    return (flg);
}

int keymatch(const char *line, const char *keyword, double *value, char **strval)
{
    /* A very general and convinient way of reading datafile and input file */
    /* find keyword in line, assign the value after keyword to value array if there is any */
    /* store both numbers and strings in order for later use, buffer required */
    /* if is keyword not found return 0. If comments, return 2. Otherwise return 1 */
    int             i;

    for (i = 0; i < WORDS_LINE; i++)
        value[i] = 0.0;

    if ((line[0] == '!') || (line[0] == '#'))
    {
        /* assign a special flag for comments */
        return (2);
    }

    int             j, k;
    int             words_line = WORDS_LINE;
    int             keyfoundflag = 0;

    char          **words;
    words = (char **)malloc(WORDS_LINE * sizeof(char *));

    for (i = 0; i < WORDS_LINE; i++)
    {
        words[i] = (char *)malloc(WORD_WIDTH * sizeof(char));
        memset(words[i], 0, WORD_WIDTH);
    }
    i = j = k = 0;

    /* Partition the line into words */
    while (i < (int)strlen(line))
    {
        if (line[i] != 39)
        {
            while (line[i] != 9 && line[i] != 0 && line[i] != 10
                && line[i] != 32 && line[i] != 13)
            {
                words[k][j++] = line[i++];
                if (line[i] == 9 || line[i] == 32 || line[i] == 13)
                {
                    k++;
                    j = 0;
                }
            }
        }
        else
        {
            words[k][j++] = line[i++];
            while (line[i] != 39)
            {
                words[k][j++] = line[i++];
            }
            words[k++][j] = line[i++];
            j = 0;
        }
        i++;
    }

    words_line = k + 1;

    for (i = 0; i < words_line; i++)
        if (strcmp(words[i], keyword) == 0)
            keyfoundflag = 1;

    j = k = 0;
    for (i = 0; i < words_line; i++)
    {
        strcpy(strval[k++], words[i]);
        if (realcheck(words[i]) == 1)
            value[j++] = atof(words[i]);
    }

    for (i = 0; i < WORDS_LINE; i++)
        free(words[i]);
    free(words);
    return (keyfoundflag);

}

void chem_alloc(char *filename, const pihm_struct pihm, Chem_Data CD)
{
    int             i, j, k;
    int             num_species, num_mineral, num_ads, num_cex, num_other,
        num_conditions = 0;
    int             line_width = LINE_WIDTH, words_line =
        WORDS_LINE, word_width = WORD_WIDTH;
    int             Global_diff = 0, Global_disp = 0;
    int             speciation_flg = 0, specflg;
    double          total_area = 0.0, tmpval[WORDS_LINE];
    char            cmdstr[MAXSTRING];
    int             lno = 0;
    int             PRCP_VOL;
    int             VIRTUAL_VOL;

    assert(pihm != NULL);

    char            line[256];
    char          **tmpstr = (char **)malloc(WORDS_LINE * sizeof(char *));

    for (i = 0; i < words_line; i++)
        tmpstr[i] = (char *)malloc(WORD_WIDTH * sizeof(char));

    char           *chemfn =
        (char *)malloc((strlen(filename) * 2 + 100) * sizeof(char));
    sprintf(chemfn, "input/%s/%s.chem", filename, filename);
    FILE           *chemfile = fopen(chemfn, "r");

    char           *datafn =
        (char *)malloc((strlen(filename) * 2 + 100) * sizeof(char));
    sprintf(datafn, "input/%s/%s.cdbs", filename, filename);
    FILE           *database = fopen(datafn, "r");

    char           *forcfn =
        (char *)malloc((strlen(filename) * 2 + 100) * sizeof(char));
    sprintf(forcfn, "input/%s/%s.prep", filename, filename);
    FILE           *prepconc = fopen(forcfn, "r");

    char           *maxwaterfn =
        (char *)malloc((strlen(filename) * 2 + 100) * sizeof(char));
    sprintf(maxwaterfn, "input/%s/%s.maxwater", filename, filename);
    FILE           *maxwater = fopen(maxwaterfn, "r");
    free(maxwaterfn);

    if (chemfile == NULL)
    {
        fprintf(stderr, "\n  Fatal Error: %s.chem does not exist! \n",
            filename);
        exit(1);
    }

    if (database == NULL)
    {
        fprintf(stderr, "\n  Fatal Error: %s.cdbs does not exist! \n",
            filename);
        exit(1);
    }

    if (prepconc == NULL)
    {
        fprintf(stderr, "\n  Fatal Error: %s.prep does not exist! \n",
            filename);
        exit(1);
    }

    if (maxwater == NULL)
    {
        fprintf(stderr, "\n  Fatal Error: %s.maxwater does not exist! \n",
            filename);
        exit(1);
    }

    /*
     * Begin updating variables
     */
#if defined(_FBR_)
    CD->NumVol = 4 * nelem + nriver + 2;
#else
    CD->NumVol = 2 * nelem + nriver + 2;
#endif
    CD->NumOsv = CD->NumVol - 2;
    CD->NumEle = nelem;
    CD->NumRiv = nriver;

    PRCP_VOL = CD->NumVol - 1;
    VIRTUAL_VOL = CD->NumVol;

    /* Default control variable if not found in input file */
    CD->StartTime = pihm->ctrl.starttime / 60;
    CD->TVDFlg = 1;
    CD->OutItv = 1;
    CD->Cementation = 1.0;
    CD->ACTmod = 0;
    CD->DHEdel = 0;
    CD->TEMcpl = 0;
    CD->EffAds = 0;
    CD->RelMin = 0;
    CD->AvgScl = 1;
    CD->CptFlg = 1;
    CD->TimRiv = 1.0;
    CD->React_delay = 10;
    CD->Condensation = 1.0;
    CD->NumBTC = 0;
    CD->NumPUMP = 0;
    CD->SUFEFF = 1;
    CD->CnntVelo = 0.01;
    CD->TimLst = 0.0;

    /* Reading "*.chem" */
    /* RUNTIME block */
    fprintf(stderr, "\n Reading '%s.chem' RUNTIME: \n", filename);
    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "RUNTIME", tmpval, tmpstr) != 1)
        fgets(line, line_width, chemfile);
    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        fgets(line, line_width, chemfile);
        if (keymatch(line, "tvd", tmpval, tmpstr) == 1)
        {
            if (strcmp(tmpstr[1], "false") == 0)
                CD->TVDFlg = 0;
            if (strcmp(tmpstr[1], "true") == 0)
                CD->TVDFlg = 1;
            if (strcmp(tmpstr[1], "false") && strcmp(tmpstr[1], "true"))
                fprintf(stderr, "  TVD FLAG INPUT ERROR! \n");
            fprintf(stderr, "  Total variation diminishing set to %d %s. \n",
                CD->TVDFlg, tmpstr[1]);
        }
        if (keymatch(line, "output", tmpval, tmpstr) == 1)
        {
            CD->OutItv = (int)tmpval[0];
            fprintf(stderr, "  Output interval set to %d hours. \n",
                CD->OutItv);
        }
        if (keymatch(line, "activity", tmpval, tmpstr) == 1)
        {
            CD->ACTmod = (int)tmpval[0];
            fprintf(stderr, "  Activity correction is set to %d. \n",
                CD->ACTmod);
            /* 0 for unity activity coefficient and 1 for DH equation update */
        }
        if (keymatch(line, "act_coe_delay", tmpval, tmpstr) == 1)
        {
            CD->DHEdel = (int)tmpval[0];
            fprintf(stderr,
                "  Activity coefficient update delay is set to %d. \n",
                CD->DHEdel);
            /* 0 for delay and 1 for no delay (solving together) */
        }
        if (keymatch(line, "thermo", tmpval, tmpstr) == 1)
        {
            CD->TEMcpl = (int)tmpval[0];
            fprintf(stderr, "  Coupling of thermo modelling is set to %d. \n",
                CD->DHEdel);
            /* 0 for delay and 1 for no delay (solving together) */
        }
        if (keymatch(line, "relmin", tmpval, tmpstr) == 1)
        {
            CD->RelMin = (int)tmpval[0];
            switch (CD->RelMin)
            {
                case 0:
                    fprintf(stderr,
                        "  Using absolute mineral volume fraction. \n");
                    break;
                case 1:
                    fprintf(stderr,
                        "  Using relative mineral volume fraction. \n");
                    break;
            }
        }
        if (keymatch(line, "effads", tmpval, tmpstr) == 1)
        {
            CD->EffAds = (int)tmpval[0];
            switch (CD->EffAds)
            {
                case 0:
                    fprintf(stderr, "  Using the normal adsorption model. \n");
                    break;
                case 1:
                    fprintf(stderr,
                        "  Using the coupled MIM and adsorption model. \n");
                    break;
                    /* under construction. */
            }
        }
        if (keymatch(line, "transport_only", tmpval, tmpstr) == 1)
        {
            CD->RecFlg = (int)tmpval[0];
            switch (CD->RecFlg)
            {
                case 0:
                    fprintf(stderr, "  Transport only mode disabled.\n");
                    break;
                case 1:
                    fprintf(stderr, "  Transport only mode enabled. \n");
                    break;
                    /* under construction. */
            }
        }
        if (keymatch(line, "precipitation", tmpval, tmpstr) == 1)
        {
            CD->PrpFlg = (int)tmpval[0];
            switch (CD->PrpFlg)
            {
                case 0:
                    fprintf(stderr, "  No precipitation condition. \n");
                    break;
                case 1:
                    fprintf(stderr,
                        "  Precipitation condition is to be specified. \n");
                    break;
                case 2:
                    fprintf(stderr,
                        "  Precipitation condition is specified via file *.prep. \n");
                    break;
                    /* under construction. */
            }
        }
        if (keymatch(line, "RT_delay", tmpval, tmpstr) == 1)
        {
            CD->Delay = (int)tmpval[0];
            fprintf(stderr,
                "  Flux-PIHM-RT will start after running PIHM for %d days. \n",
                CD->Delay);
            CD->Delay *= UNIT_C;
            /* under construction. */
        }
        if (keymatch(line, "Condensation", tmpval, tmpstr) == 1)
        {
            CD->Condensation = tmpval[0];
            fprintf(stderr,
                "  The concentrations of infiltrating rainfall is set to be %f times of concentrations in precipitation. \n",
                CD->Condensation);
            /* under construction. */
            //CD->Condensation *= CS->Cal.Prep_conc;  // 09.25 temporal comment-out
            fprintf(stderr,
                "  The concentrations of infiltrating rainfall is set to be %f times of concentrations in precipitation. \n",
                CD->Condensation);
        }
        if (keymatch(line, "AvgScl", tmpval, tmpstr) == 1)
        {
            CD->React_delay = tmpval[0];
            fprintf(stderr,
                "  Averaging window for asynchronous reaction %d. \n",
                CD->React_delay);
            /* under construction. */
        }
        if (keymatch(line, "SUFEFF", tmpval, tmpstr) == 1)
        {
            CD->SUFEFF = tmpval[0];
            fprintf(stderr, "  Effective surface area mode set to %d. \n\n",
                CD->SUFEFF);
            /* under construction. */
        }
        if (keymatch(line, "Mobile_exchange", tmpval, tmpstr) == 1)
        {
            CD->TimRiv = tmpval[0];
            fprintf(stderr, "  Ratio of immobile ion exchange site %f. \n",
                CD->TimRiv);
            /* under construction. */
        }

        if (keymatch(line, "Connectivity_threshold", tmpval, tmpstr) == 1)
        {
            CD->CnntVelo = tmpval[0];
            fprintf(stderr,
                "  Minimum velocity to be deemed as connected is %f m/d. \n",
                CD->CnntVelo);
            /* under construction. */
        }
    }

    /* OUTPUT block */
    fprintf(stderr, "\n Reading '%s.chem' OUTPUT: \n", filename);
    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while ((keymatch(line, "OUTPUT", tmpval, tmpstr) != 1) && (!feof(chemfile)))
        fgets(line, line_width, chemfile);
    CD->NumBTC = tmpval[0];
    fprintf(stderr, "  %d breakthrough points specified. \n", CD->NumBTC);
    CD->BTC_loc = (int *)malloc(CD->NumBTC * sizeof(int));
    i = 0;
    fprintf(stderr, "  --");
    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        fgets(line, line_width, chemfile);
        if (keymatch(line, " ", tmpval, tmpstr) != 2)
        {
            CD->BTC_loc[i] = (int)tmpval[0] - 1;
            fprintf(stderr, " Grid %d ", CD->BTC_loc[i] + 1);
            i++;
        }
        if (i >= CD->NumBTC)
            break;
    }
    fprintf(stderr, "are breakthrough points.\n\n");

    /* GLOBAL block */
    fprintf(stderr, " Reading '%s.chem' GLOBAL: \n", filename);
    species         Global_type;
    Global_type.ChemName = (char *)malloc(WORD_WIDTH * sizeof(char));
    strcpy(Global_type.ChemName, "GLOBAL");

    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "GLOBAL", tmpval, tmpstr) != 1)
        fgets(line, line_width, chemfile);
    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        fgets(line, line_width, chemfile);
        if (keymatch(line, "t_species", tmpval, tmpstr) == 1)
        {
            CD->NumStc = (int)tmpval[0];
            fprintf(stderr, "  %d chemical species specified. \n", CD->NumStc);
            /* H2O is always a primary species */
        }
        if (keymatch(line, "s_species", tmpval, tmpstr) == 1)
        {
            CD->NumSsc = (int)tmpval[0];
            fprintf(stderr, "  %d secondary species specified. \n",
                (int)tmpval[0]);
        }
        if (keymatch(line, "minerals", tmpval, tmpstr) == 1)
        {
            CD->NumMin = (int)tmpval[0];
            fprintf(stderr, "  %d minerals specified. \n", CD->NumMin);
        }
        if (keymatch(line, "adsorption", tmpval, tmpstr) == 1)
        {
            CD->NumAds = (int)tmpval[0];
            fprintf(stderr, "  %d surface complexation specified. \n",
                CD->NumAds);
        }
        if (keymatch(line, "cation_exchange", tmpval, tmpstr) == 1)
        {
            CD->NumCex = (int)tmpval[0];
            fprintf(stderr, "  %d cation exchange specified. \n", CD->NumCex);
        }
        if (keymatch(line, "mineral_kinetic", tmpval, tmpstr) == 1)
        {
            CD->NumMkr = (int)tmpval[0];
            fprintf(stderr, "  %d mineral kinetic reaction(s) specified. \n",
                CD->NumMkr);
        }
        if (keymatch(line, "aqueous_kinetic", tmpval, tmpstr) == 1)
        {
            CD->NumAkr = (int)tmpval[0];
            fprintf(stderr, "  %d aqueous kinetic reaction(s) specified. \n",
                CD->NumAkr);
        }
        if (keymatch(line, "diffusion", tmpval, tmpstr) == 1)
        {
            fprintf(stderr, "  Diffusion coefficient = %g [cm2/s] \n",
                tmpval[0]);
            Global_type.DiffCoe = tmpval[0] * 60.0 * 60.0 * 24.0 / 10000.0;
            Global_diff = 1;
            /* Require unit conversion ! */
        }
        if (keymatch(line, "dispersion", tmpval, tmpstr) == 1)
        {
            fprintf(stderr, "  Dispersion coefficient = %2.2f [m] \n",
                tmpval[0]);
            Global_type.DispCoe = tmpval[0];
            Global_disp = 1;
            /* Set global flags to indicate the global values are present */
        }
        if (keymatch(line, "cementation", tmpval, tmpstr) == 1)
        {
            fprintf(stderr, "  Cementation factor = %2.1f \n", tmpval[0]);
            CD->Cementation = tmpval[0];
        }
        if (keymatch(line, "temperature", tmpval, tmpstr) == 1)
        {
            CD->Temperature = tmpval[0];
            fprintf(stderr, "  Temperature = %3.1f \n\n", CD->Temperature);
        }
    }

    /* The number of species that are mobile, later used in the OS3D subroutine */
    CD->NumSpc = CD->NumStc - (CD->NumMin + CD->NumAds + CD->NumCex);

    /* The number of species that others depend on */
    CD->NumSdc = CD->NumStc - CD->NumMin;

    CD->Dependency = (double **)malloc(CD->NumSsc * sizeof(double *));
    for (i = 0; i < CD->NumSsc; i++)
    {
        CD->Dependency[i] = (double *)malloc(CD->NumSdc * sizeof(double));
        /* Convert secondary species as an expression of primary species */
        for (j = 0; j < CD->NumSdc; j++)
            CD->Dependency[i][j] = 0.0;
    }

    CD->Dep_kinetic =
        (double **)malloc((CD->NumMkr + CD->NumAkr) * sizeof(double *));
    for (i = 0; i < CD->NumMkr + CD->NumAkr; i++)
    {
        CD->Dep_kinetic[i] = (double *)malloc(CD->NumStc * sizeof(double));
        /* Express kinetic species as function of primary species */
        for (j = 0; j < CD->NumStc; j++)
            CD->Dep_kinetic[i][j] = 0.0;
    }

    CD->Dep_kinetic_all = (double **)malloc((CD->NumMin) * sizeof(double *));
    for (i = 0; i < CD->NumMin; i++)
    {
        CD->Dep_kinetic_all[i] = (double *)malloc(CD->NumStc * sizeof(double));
        /* Dependencies of minearls, all */
        for (j = 0; j < CD->NumStc; j++)
            CD->Dep_kinetic_all[i][j] = 0.0;
    }

    /* Keqs of equilibrium/ kinetic and kinetic all */
    CD->Keq = (double *)malloc(CD->NumSsc * sizeof(double));
    CD->KeqKinect =
        (double *)malloc((CD->NumMkr + CD->NumAkr) * sizeof(double));
    CD->KeqKinect_all = (double *)malloc(CD->NumMin * sizeof(double));

    /* Convert total concentration as an expression of all species */
    CD->Totalconc = (double **)malloc(CD->NumStc * sizeof(double *));
    for (i = 0; i < CD->NumStc; i++)
        CD->Totalconc[i] =
            (double *)malloc((CD->NumStc + CD->NumSsc) * sizeof(double));

#if NOT_YET_IMPLEMENTED
    /* Convert total concentration as an expression of all species */
    CD->Totalconck = (double **)malloc(CD->NumStc * sizeof(double *));
    for (i = 0; i < CD->NumStc; i++)
        CD->Totalconck[i] =
            (double *)malloc((CD->NumStc + CD->NumSsc) * sizeof(double));
#endif

    for (i = 0; i < CD->NumStc; i++)
        for (j = 0; j < CD->NumStc + CD->NumSsc; j++)
        {
            CD->Totalconc[i][j] = 0.0;
#if NOT_YET_IMPLEMENTED
            CD->Totalconck[i][j] = 0.0;
#endif
        }

    /* INITIAL_CONDITIONS block */
    fprintf(stderr, " Reading '%s.chem' INITIAL_CONDITIONS: \n", filename);
    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "INITIAL_CONDITIONS", tmpval, tmpstr) != 1)
        fgets(line, line_width, chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        if (keymatch(line, " ", tmpval, tmpstr) != 2)
        {
            num_conditions++;
        }
        fgets(line, line_width, chemfile);
    }
    fprintf(stderr, "  %d conditions assigned. \n", num_conditions);

    char          **chemcon = (char **)malloc(num_conditions * sizeof(char *));
    for (i = 0; i < num_conditions; i++)
        chemcon[i] = (char *)malloc(word_width * sizeof(char));
    char         ***con_chem_name =
        (char ***)malloc((num_conditions + 1) * sizeof(char **));
    for (i = 0; i < num_conditions + 1; i++)
    {   /* all conditions + precipitation */
        con_chem_name[i] = (char **)malloc(CD->NumStc * sizeof(char *));
        for (j = 0; j < CD->NumStc; j++)
            con_chem_name[i][j] = (char *)malloc(WORD_WIDTH * sizeof(char));
    }

    int            *condition_index = (int *)malloc(CD->NumVol * sizeof(int));
    /* When user assign conditions to blocks, they start from 1 */

    for (i = 0; i < CD->NumVol; i++)
    {
        condition_index[i] = 0;
    }

    vol_conc       *Condition_vcele =
        (vol_conc *) malloc(num_conditions * sizeof(vol_conc));
    for (i = 0; i < num_conditions; i++)
    {
        Condition_vcele[i].index = i + 1;
        Condition_vcele[i].t_conc =
            (double *)malloc(CD->NumStc * sizeof(double));
        Condition_vcele[i].p_conc =
            (double *)malloc(CD->NumStc * sizeof(double));
        Condition_vcele[i].p_para =
            (double *)malloc(CD->NumStc * sizeof(double));
        Condition_vcele[i].p_type = (int *)malloc(CD->NumStc * sizeof(int));
        Condition_vcele[i].s_conc = NULL;
        /* We do not input cocentration for secondary speices in rt */
        for (j = 0; j < CD->NumStc; j++)
        {
            Condition_vcele[i].t_conc[j] = ZERO;
            Condition_vcele[i].p_conc[j] = ZERO;
        }
    }

    if (CD->PrpFlg)
    {
        CD->Precipitation.t_conc =
            (double *)malloc(CD->NumStc * sizeof(double));
        CD->Precipitation.p_conc =
            (double *)malloc(CD->NumStc * sizeof(double));
        CD->Precipitation.p_para =
            (double *)malloc(CD->NumStc * sizeof(double));
        CD->Precipitation.p_type = (int *)malloc(CD->NumStc * sizeof(int));
        CD->Precipitation.s_conc = NULL;
        for (i = 0; i < CD->NumStc; i++)
        {
            CD->Precipitation.t_conc[i] = ZERO;
            CD->Precipitation.p_conc[i] = ZERO;
        }
    }

    CD->chemtype =
        (species *) malloc((CD->NumStc + CD->NumSsc) * sizeof(species));
    if (CD->chemtype == NULL)
        fprintf(stderr, " Memory allocation error\n");

    for (i = 0; i < CD->NumStc + CD->NumSsc; i++)
    {
        if (Global_diff == 1)
            CD->chemtype[i].DiffCoe = Global_type.DiffCoe;
        /*
         * else
         * CD->chemtype[i].DiffCoe = ZERO;
         */
        /* in squre m per day */
        if (Global_disp == 1)
            CD->chemtype[i].DispCoe = Global_type.DispCoe;
        else
            CD->chemtype[i].DispCoe = ZERO;

        CD->chemtype[i].ChemName = (char *)malloc(WORD_WIDTH * sizeof(char));
        assert(CD->chemtype[i].ChemName != NULL);
        memset(CD->chemtype[i].ChemName, 0, WORD_WIDTH);
        CD->chemtype[i].Charge = 0.0;
        CD->chemtype[i].SizeF = 1.0;
        CD->chemtype[i].itype = 0;
    }

    k = 0;
    int             initfile = 0;
    FILE           *cheminitfile = NULL;
    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "INITIAL_CONDITIONS", tmpval, tmpstr) != 1)
        fgets(line, line_width, chemfile);
    if (strcmp(tmpstr[1], "FILE") == 0)
    {
        /* Initialize chemical distribution from file evoked. This will nullify
         * all the condition assignment given in the next lines.
         * But for now, please keep those lines to let the code work. */

        initfile = 1;
        fprintf(stderr, "  Specifiying the initial chemical distribution from file '%s.cini'. \n", filename);

        char           *cheminit =
            (char *)malloc((strlen(filename) * 2 + 100) * sizeof(char));
        sprintf(cheminit, "input/%s/%s.cini", filename, filename);
        cheminitfile = fopen(cheminit, "r");

        if (cheminitfile == NULL)
        {
            fprintf(stderr, "  Fatal Error: %s.cini does not exist! \n",
                filename);
            exit(1);
        }
        else
        {
            fprintf(stderr, "  Reading the '%s.cini'!! \n", filename);
        }

        free(cheminit);         // 10.02
    }

    fgets(line, line_width, chemfile);
    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        if (keymatch(line, " ", tmpval, tmpstr) != 2)
        {
            strcpy(chemcon[k++], tmpstr[0]);
            if (initfile == 0)
            {
                PIHMprintf(VL_ERROR,
                    "Assigning initial conditions in .chem file is temporarily"
                    " disabled. Please use a .cini file.\n");
                PIHMexit(EXIT_FAILURE);
            }
        }
        fgets(line, line_width, chemfile);
    }
    if (initfile == 1)
    {
        for (i = 0; i < CD->NumVol; i++)
        {
            fscanf(cheminitfile, "%d %d", &k, &condition_index[i]);
        }
    }

    if (cheminitfile != NULL)
        fclose(cheminitfile);

    /* CONDITIONS block */
    fprintf(stderr, "\n Reading '%s.chem' CONDITIONS: ", filename);
    for (i = 0; i < num_conditions; i++)
    {
        rewind(chemfile);
        num_species = 0;
        num_mineral = 0;
        num_ads = 0;
        num_cex = 0;
        num_other = 0;
        fgets(line, line_width, chemfile);
        while ((keymatch(line, "Condition", tmpval, tmpstr) != 1) ||
            (keymatch(line, chemcon[i], tmpval, tmpstr) != 1))
            fgets(line, line_width, chemfile);
        if (strcmp(tmpstr[1], chemcon[i]) == 0)
            fprintf(stderr, "\n  %s", line);
        fgets(line, line_width, chemfile);
        while (keymatch(line, "END", tmpval, tmpstr) != 1)
        {
            if (keymatch(line, "NULL", tmpval, tmpstr) != 2)
            {
                specflg = SpeciationType(database, tmpstr[0]);

                if (specflg == AQUEOUS)
                {
                    /* Arrange the concentration of the primary species in such a
                     * way that all the mobile species are at the beginning. */
                    num_other = num_mineral + num_ads + num_cex;
                    Condition_vcele[i].t_conc[num_species - num_other] =
                        tmpval[0];
                    strcpy(con_chem_name[i][num_species - num_other],
                        tmpstr[0]);
                    fprintf(stderr, "  %-28s %g \n",
                        con_chem_name[i][num_species - num_other], tmpval[0]);
                    Condition_vcele[i].p_type[num_species - num_other] = AQUEOUS;
                }
                if (specflg == MINERAL)
                {
                    Condition_vcele[i].t_conc[CD->NumSpc + CD->NumAds +
                        CD->NumCex + num_mineral] = tmpval[0];
                    if (strcmp(tmpstr[2], "-ssa") == 0)
                        Condition_vcele[i].p_para[CD->NumSpc + CD->NumAds +
                            CD->NumCex + num_mineral] = tmpval[1] * 1.0;
                    strcpy(con_chem_name[i][CD->NumSpc + CD->NumAds +
                            CD->NumCex + num_mineral], tmpstr[0]);
                    fprintf(stderr,
                        "  mineral %-20s %6.4f \t specific surface area \t%6.4f \n",
                        con_chem_name[i][CD->NumSpc + CD->NumAds + CD->NumCex +
                            num_mineral], tmpval[0], tmpval[1]);
                    Condition_vcele[i].p_type[CD->NumSpc + CD->NumAds +
                        CD->NumCex + num_mineral] = MINERAL;
                    num_mineral++;
                }
                if ((tmpstr[0][0] == '>') || (specflg == ADSORPTION))
                {
                    /* Adsorptive sites and species start with > */
                    /* Condition_vcele[i].t_conc[CD->NumSpc + num_ads] = tmpval[0] * CS->Cal.Site_den;  09.25 temporal comment-out */
                    Condition_vcele[i].t_conc[CD->NumSpc + num_ads] =
                        tmpval[0] * 1.0;
                    Condition_vcele[i].p_type[CD->NumSpc + num_ads] = ADSORPTION;
                    Condition_vcele[i].p_para[CD->NumSpc + num_ads] = 0;
                    /* Update when fill in the parameters for adsorption */
                    strcpy(con_chem_name[i][CD->NumSpc + num_ads], tmpstr[0]);
                    fprintf(stderr, "  surface complex %s\t\t%6.4f \n",
                        con_chem_name[i][CD->NumSpc + num_ads], tmpval[0]);
                    num_ads++;
                    /* under construction */
                }
                if (specflg == CATION_ECHG)
                {
                    Condition_vcele[i].t_conc[CD->NumSpc + CD->NumAds +
                        num_cex] = tmpval[0];
                    Condition_vcele[i].p_type[CD->NumSpc + CD->NumAds +
                        num_cex] = CATION_ECHG;
                    Condition_vcele[i].p_para[CD->NumSpc + CD->NumAds +
                        num_cex] = 0;
                    /* update when fill in the parameters for cation exchange. */
                    strcpy(con_chem_name[i][CD->NumSpc + CD->NumAds + num_cex],
                        tmpstr[0]);
                    fprintf(stderr, "  cation exchange %s\t\t%6.4f \n",
                        con_chem_name[i][CD->NumSpc + CD->NumAds + num_cex],
                        tmpval[0]);
                    num_cex++;
                    /* under construction */
                }
                num_species++;
            }
            fgets(line, line_width, chemfile);
        }
    }

    /* PRECIPITATION block */
    fprintf(stderr, "\n Reading '%s.chem' PRECIPITATION: ", filename);
    if (CD->PrpFlg)
    {
        rewind(chemfile);
        fgets(line, line_width, chemfile);
        while (keymatch(line, "PRECIPITATION", tmpval, tmpstr) != 1)
            fgets(line, line_width, chemfile);
        fgets(line, line_width, chemfile);
        fprintf(stderr, " \n");
        fprintf(stderr, "  ---------------------------------\n");
        fprintf(stderr, "  The condition of precipitation is \n");
        fprintf(stderr, "  ---------------------------------\n");
        num_species = 0;
        num_mineral = 0;
        num_ads = 0;
        num_cex = 0;
        num_other = 0;
        while (keymatch(line, "END", tmpval, tmpstr) != 1)
        {
            if (keymatch(line, "NULL", tmpval, tmpstr) != 2)
            {
                specflg = SpeciationType(database, tmpstr[0]);

                if (specflg == AQUEOUS)
                {
                    num_other = num_mineral + num_ads + num_cex;
                    CD->Precipitation.t_conc[num_species - num_other] =
                        tmpval[0];
                    strcpy(con_chem_name[num_conditions][num_species -
                            num_other], tmpstr[0]);
                    fprintf(stderr, "  %-28s %g \n",
                        con_chem_name[num_conditions][num_species - num_other],
                        tmpval[0]);
                    CD->Precipitation.p_type[num_species - num_other] = AQUEOUS;
                }
                /* arrange the concentration of the primary species in such a
                 * way that all the mobile species are at the beginning. */
                if (specflg == MINERAL)
                {
                    CD->Precipitation.t_conc[CD->NumSpc + CD->NumAds +
                        CD->NumCex + num_mineral] = tmpval[0];
                    if (strcmp(tmpstr[2], "-ssa") == 0)
                        CD->Precipitation.p_para[CD->NumSpc + CD->NumAds +
                            CD->NumCex + num_mineral] = tmpval[1];
                    strcpy(con_chem_name[num_conditions][CD->NumSpc +
                            CD->NumAds + CD->NumCex + num_mineral], tmpstr[0]);
                    fprintf(stderr,
                        "  mineral %-20s %6.4f \t specific surface area %6.4f\n",
                        con_chem_name[num_conditions][CD->NumSpc + CD->NumAds +
                            CD->NumCex + num_mineral], tmpval[0], tmpval[1]);
                    CD->Precipitation.p_type[CD->NumSpc + CD->NumAds +
                        CD->NumCex + num_mineral] = MINERAL;
                    num_mineral++;
                }
                if ((tmpstr[0][0] == '>') || (specflg == ADSORPTION))
                {
                    /* adsorptive sites and species start with > */
                    CD->Precipitation.t_conc[CD->NumSpc + num_ads] =
                        tmpval[0]; /* this is the site density of the adsorptive species. */
                    CD->Precipitation.p_type[CD->NumSpc + num_ads] = ADSORPTION;
                    CD->Precipitation.p_para[CD->NumSpc + num_ads] = 0;
                    /* Update when fill in the parameters for adsorption. */
                    strcpy(con_chem_name[num_conditions][CD->NumSpc + num_ads],
                        tmpstr[0]);
                    fprintf(stderr, " surface complex %s\t %6.4f\n",
                        con_chem_name[num_conditions][CD->NumSpc + num_ads],
                        tmpval[0]);
                    num_ads++;
                    /* under construction */
                }
                if (specflg == CATION_ECHG)
                {
                    CD->Precipitation.t_conc[CD->NumSpc + CD->NumAds +
                        num_cex] = tmpval[0];
                    CD->Precipitation.p_type[CD->NumSpc + CD->NumAds +
                        num_cex] = CATION_ECHG;
                    CD->Precipitation.p_para[CD->NumSpc + CD->NumAds +
                        num_cex] = 0;
                    /* Update when fill in the parameters for cation exchange. */
                    strcpy(con_chem_name[num_conditions][CD->NumSpc +
                            CD->NumAds + num_cex], tmpstr[0]);
                    fprintf(stderr, " cation exchange %s\t %6.4f\n",
                        con_chem_name[num_conditions][CD->NumSpc + CD->NumAds +
                            num_cex], tmpval[0]);
                    num_cex++;
                    /* under construction */
                }
                num_species++;
            }
            fgets(line, line_width, chemfile);
        }
    }

    int             check_conditions_num;

    if (CD->PrpFlg)
        check_conditions_num = num_conditions + 1;
    else
        check_conditions_num = num_conditions;

    if (num_species != CD->NumStc)
        fprintf(stderr, " Number of species does not match indicated value!\n");

    for (i = 1; i < check_conditions_num; i++)
    {
        for (j = 0; j < num_species; j++)
        {
            if (strcmp(con_chem_name[i][j], con_chem_name[i - 1][j]) != 0)
            {
                fprintf(stderr,
                    " The order of the chemicals in condition <%s> is incorrect!\n",
                    chemcon[i - 1]);
            }
        }
    }

    /* Primary species table */
    fprintf(stderr,
        "\n Primary species and their types: [1], aqueous; [2], adsorption; [3], cation exchange; [4], mineral. \n");
    /* Number of total species in the rt simulator */
    for (i = 0; i < CD->NumStc; i++)
    {
        strcpy(CD->chemtype[i].ChemName, con_chem_name[0][i]);
        CD->chemtype[i].itype = Condition_vcele[0].p_type[i];
        fprintf(stderr, "  %-20s %10d\n", CD->chemtype[i].ChemName,
            CD->chemtype[i].itype);
    }

    /* Precipitation conc table */
    if (CD->PrpFlg)
    {
        fprintf(stderr, "\n Total concentraions in precipitataion: \n");
        for (i = 0; i < CD->NumSpc; i++)
        {
            if (!strcmp(con_chem_name[num_conditions][i], "pH"))
            {
                if (CD->Precipitation.t_conc[i] < 7)
                {
                    CD->Precipitation.t_conc[i] =
                        pow(10, -CD->Precipitation.t_conc[i]);
                }
                else
                {
                    CD->Precipitation.t_conc[i] =
                        -pow(10, CD->Precipitation.t_conc[i] - 14);
                }
            }
            /* Change the pH of precipitation into total concentraion of H
             * We skip the speciation for rain and assume it is OK to calculate
             * this way. */
            fprintf(stderr, "  %-20s %-10.3g [M] \n",
                con_chem_name[num_conditions][i], CD->Precipitation.t_conc[i]);
        }
    }

    /* SECONDARY_SPECIES block */
    fprintf(stderr, "\n Reading 'shp.chem' SECONDARY_SPECIES: \n");
    fprintf(stderr, "  Secondary species specified in the input file: \n");
    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "SECONDARY_SPECIES", tmpval, tmpstr) != 1)
        fgets(line, line_width, chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        if (keymatch(line, "NULL", tmpval, tmpstr) != 2)
        {
            strcpy(CD->chemtype[num_species++].ChemName, tmpstr[0]);
            fprintf(stderr, "  %s \n", CD->chemtype[num_species - 1].ChemName);
        }
        fgets(line, line_width, chemfile);
    }

    /* MINERALS block */
    fprintf(stderr, "\n Reading 'shp.chem' MINERALS: \n");

    CD->kinetics =
        (Kinetic_Reaction *) malloc(CD->NumMkr * sizeof(Kinetic_Reaction));
    for (i = 0; i < CD->NumMkr; i++)
    {
        for (j = 0; j < MAXDEP; j++)
        {
            CD->kinetics[i].dep_position[j] = 0;
            CD->kinetics[i].monod_position[j] = 0;  // 08.19
            CD->kinetics[i].inhib_position[j] = 0;  // 08.19
        }
    }

    k = 0;
    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "MINERALS", tmpval, tmpstr) != 1)
        fgets(line, line_width, chemfile);
    fgets(line, line_width, chemfile);
    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        if (keymatch(line, " ", tmpval, tmpstr) != 2)
        {
            strcpy(CD->kinetics[k].species, tmpstr[0]);
            if (strcmp(tmpstr[1], "-label") == 0)
                strcpy(CD->kinetics[k].Label, tmpstr[2]);
            k++;
        }
        fgets(line, line_width, chemfile);
    }

    for (i = 0; i < k; i++)
        fprintf(stderr,
            "  Kinetic reaction on '%s' is specified, label '%s'. \n",
            CD->kinetics[i].species, CD->kinetics[i].Label);

    /* Precipitation conc read in */
    fprintf(stderr, "\n Reading 'shp.prep': \n");
    if (CD->PrpFlg == 2)
    {
        CD->TSD_prepconc = (tsdata_struct *)malloc(sizeof(tsdata_struct));
        fscanf(prepconc, "%*s %d %d",
            &(CD->TSD_prepconc[0].nspec), &(CD->TSD_prepconc[0].length));

        CD->prepconcindex =
            (int *)malloc(CD->TSD_prepconc[0].nspec * sizeof(int));
        /* The number of primary species must be equal to the number of primary
         * species specified before. */
        for (i = 0; i < CD->TSD_prepconc[0].nspec; i++)
        {
            fscanf(prepconc, "%d", &(CD->prepconcindex[i]));
            if (CD->prepconcindex[i] > 0)
            {
                assert(CD->prepconcindex[i] <= CD->NumSpc);
                fprintf(stderr,
                    "  Precipitation conc of '%s' is a time series. \n",
                    CD->chemtype[CD->prepconcindex[i] - 1].ChemName);
            }
        }

        CD->TSD_prepconc[0].ftime =
            (int *)malloc((CD->TSD_prepconc[0].length) * sizeof(int));
        CD->TSD_prepconc[0].data =
            (double **)malloc((CD->TSD_prepconc[0].length) * sizeof(double *));
        CD->TSD_prepconc[0].value =
            (double *)malloc(CD->TSD_prepconc[0].nspec * sizeof(double));
        for (i = 0; i < CD->TSD_prepconc[0].length; i++)
        {
            CD->TSD_prepconc[0].data[i] =
                (double *)malloc(CD->TSD_prepconc[0].nspec * sizeof(double));

            NextLine(prepconc, cmdstr, &lno);
            ReadTS(cmdstr, &CD->TSD_prepconc[0].ftime[i],
                &CD->TSD_prepconc[0].data[i][0], CD->TSD_prepconc[0].nspec);
        }

        /* Convert pH to H+ concentration */
        for (i = 0; i < CD->TSD_prepconc[0].nspec; i++)
        {
            if (CD->prepconcindex[i] > 0 &&
                !strcmp(con_chem_name[num_conditions][CD->prepconcindex[i] - 1],
                "pH"))
            {
                for (k = 0; k < CD->TSD_prepconc[0].length; k++)
                {
                    CD->TSD_prepconc[0].data[k][i] =
                        (CD->TSD_prepconc[0].data[k][i] < 7.0) ?
                        pow(10, -CD->TSD_prepconc[0].data[k][i]) :
                        -pow(10, -CD->TSD_prepconc[0].data[k][i] - 14);
                }
                break;
            }
        }
    }

    /* PUMP block */
    CD->CalGwinflux = pihm->cal.gwinflux;

    fprintf(stderr, "\n Reading 'shp.chem' PUMP: \n");
    rewind(chemfile);
    fgets(line, line_width, chemfile);
    while ((keymatch(line, "PUMP", tmpval, tmpstr) != 1) && (!feof(chemfile)))
        fgets(line, line_width, chemfile);
    CD->NumPUMP = tmpval[0];
    fprintf(stderr, "  %d pumps specified. \n", CD->NumPUMP);
    CD->pumps = (Pump *) malloc(CD->NumPUMP * sizeof(Pump));
    i = 0;

    while (keymatch(line, "END", tmpval, tmpstr) != 1)
    {
        fgets(line, line_width, chemfile);
        if (keymatch(line, " ", tmpval, tmpstr) != 2)
        {
            CD->pumps[i].Pump_Location = (int)tmpval[0];
            CD->pumps[i].Injection_rate = (double)tmpval[1];
            CD->pumps[i].Injection_conc = (double)tmpval[2];
            CD->pumps[i].flow_rate =
                CD->pumps[i].Injection_rate / CD->pumps[i].Injection_conc /
                365 * 1E-3;
            CD->pumps[i].Name_Species = (char *)malloc(20 * sizeof(char));
            strcpy(CD->pumps[i].Name_Species, tmpstr[1]);
            //      wrap(CD->pumps[i].Name_Species);
            CD->pumps[i].Position_Species = -1;
            for (j = 0; j < CD->NumStc; j++)
            {
                if (!strcmp(CD->pumps[i].Name_Species,
                        CD->chemtype[j].ChemName))
                {
                    CD->pumps[i].Position_Species = j;
                }
            }

            fprintf(stderr,
                "  -- Rate %g [moles/year] of '%s' (pos: %d) at Grid '%d' with a concentration of %g [M/L]. \n",
                CD->pumps[i].Injection_rate, CD->pumps[i].Name_Species,
                (CD->pumps[i].Position_Species + 1), CD->pumps[i].Pump_Location,
                CD->pumps[i].Injection_conc);
            fprintf(stderr, "  -- Flow rate is then %g [m3/d]. \n",
                CD->pumps[i].flow_rate);
            //  CD->pumps[i].Injection_rate *= 1E-3 / 365;

            /* 02.12 calibration */
            CD->pumps[i].Injection_rate =
                CD->pumps[i].Injection_rate * CD->CalGwinflux;
            CD->pumps[i].flow_rate = CD->pumps[i].flow_rate * CD->CalGwinflux;
            fprintf(stderr,
                "  -- after calibration: injection_rate %g [moles/year], flow _rate %g [m3/d], CD->CalGwinflux = %f. \n",
                CD->pumps[i].Injection_rate, CD->pumps[i].flow_rate,
                CD->CalGwinflux);
            i++;
        }
        if (i >= CD->NumPUMP)
            break;
    }
    /* End of reading input files */

    /* Reading '*.maxwater' input file */
    fprintf(stderr, "\n Reading 'coalcreek_952.maxwater': \n");
    CD->Vcele = (vol_conc *) malloc(CD->NumVol * sizeof(vol_conc));
    for (i = 0; i < CD->NumVol; i++)
    {
        CD->Vcele[i].maxwater = 0;  /* Initialize, including ghost cells */
    }

    fscanf(maxwater, "%*[^\n]%*c"); /* Jump over the first header line */

    for (i = 0; i < nelem; i++) /* GW cells */
    {
        fscanf(maxwater, "%*d %lf", &(CD->Vcele[RT_GW(i)].maxwater));
        CD->Vcele[RT_UNSAT(i)].maxwater = CD->Vcele[RT_GW(i)].maxwater;
    }

    fclose(maxwater);

    /* Initializing volumetric parameters, inherit from PIHM
     * That is, if PIHM is started from a hot start, rt is also
     * initialized with the hot data */
    for (i = 0; i < nelem; i++)
    {
        /* Initializing volumetrics for groundwater (GW) cells */
        CD->Vcele[RT_GW(i)].height_o = pihm->elem[i].ws.gw;
        CD->Vcele[RT_GW(i)].height_t = pihm->elem[i].ws.gw;
        CD->Vcele[RT_GW(i)].area = pihm->elem[i].topo.area;
        CD->Vcele[RT_GW(i)].porosity = pihm->elem[i].soil.smcmax;
        CD->Vcele[RT_GW(i)].vol_o = pihm->elem[i].topo.area * pihm->elem[i].ws.gw;
        CD->Vcele[RT_GW(i)].vol = pihm->elem[i].topo.area * pihm->elem[i].ws.gw;
        CD->Vcele[RT_GW(i)].sat = 1.0;
        CD->Vcele[RT_GW(i)].type = GW_VOL;

        /* Initializing volumetrics for unsaturated cells */
        /* Porosity in PIHM is
         * Effective Porosity = Porosity - Residue Water Porosity
         * Porosity in RT is total Porosity, therefore, the water height in the
         * unsaturated zone needs be converted as well */
        CD->Vcele[RT_UNSAT(i)].height_o = (pihm->elem[i].ws.unsat *
            (pihm->elem[i].soil.smcmax - pihm->elem[i].soil.smcmin) +
            (pihm->elem[i].soil.depth - pihm->elem[i].ws.gw) *
            pihm->elem[i].soil.smcmin) / (pihm->elem[i].soil.smcmax);
        CD->Vcele[RT_UNSAT(i)].height_t = CD->Vcele[RT_UNSAT(i)].height_o;
        CD->Vcele[RT_UNSAT(i)].area = pihm->elem[i].topo.area;
        CD->Vcele[RT_UNSAT(i)].porosity = pihm->elem[i].soil.smcmax;
        /* Unsaturated zone has the same porosity as saturated zone */
        CD->Vcele[RT_UNSAT(i)].sat = CD->Vcele[RT_UNSAT(i)].height_o /
            (pihm->elem[i].soil.depth - pihm->elem[i].ws.gw);
        CD->Vcele[RT_UNSAT(i)].vol_o = pihm->elem[i].topo.area * CD->Vcele[RT_UNSAT(i)].height_o;
        CD->Vcele[RT_UNSAT(i)].vol = pihm->elem[i].topo.area * pihm->elem[i].soil.depth;
        CD->Vcele[RT_UNSAT(i)].type = UNSAT_VOL;

        /* The saturation of unsaturated zone is the Hu divided by height of
         * this cell */
        if (CD->Vcele[RT_UNSAT(i)].sat > 1.0)
            fprintf(stderr,
                "Fatal Error, Unsaturated Zone Initialization For RT Failed!\n");

#if defined(_FBR_)
        /* Initializing volumetrics for deep groundwater (FBR GW) cells */
        CD->Vcele[RT_FBR_GW(i)].height_o = pihm->elem[i].ws.fbr_gw;
        CD->Vcele[RT_FBR_GW(i)].height_t = pihm->elem[i].ws.fbr_gw;
        CD->Vcele[RT_FBR_GW(i)].area = pihm->elem[i].topo.area;
        CD->Vcele[RT_FBR_GW(i)].porosity = pihm->elem[i].geol.smcmax;
        CD->Vcele[RT_FBR_GW(i)].vol_o = pihm->elem[i].topo.area * pihm->elem[i].ws.fbr_gw;
        CD->Vcele[RT_FBR_GW(i)].vol = pihm->elem[i].topo.area * pihm->elem[i].ws.fbr_gw;
        CD->Vcele[RT_FBR_GW(i)].sat = 1.0;
        CD->Vcele[RT_FBR_GW(i)].type = FBR_GW_VOL;

        /* Initializing volumetrics for bedrock unsaturated cells */
        CD->Vcele[RT_FBR_UNSAT(i)].height_o = (pihm->elem[i].ws.fbr_unsat *
            (pihm->elem[i].geol.smcmax - pihm->elem[i].geol.smcmin) +
            (pihm->elem[i].geol.depth - pihm->elem[i].ws.fbr_gw) *
            pihm->elem[i].geol.smcmin) / (pihm->elem[i].geol.smcmax);
        CD->Vcele[RT_FBR_UNSAT(i)].height_t = CD->Vcele[RT_FBR_UNSAT(i)].height_o;
        CD->Vcele[RT_FBR_UNSAT(i)].area = pihm->elem[i].topo.area;
        CD->Vcele[RT_FBR_UNSAT(i)].porosity = pihm->elem[i].geol.smcmax;
        /* Unsaturated zone has the same porosity as saturated zone */
        CD->Vcele[RT_FBR_UNSAT(i)].sat = CD->Vcele[RT_FBR_UNSAT(i)].height_o /
            (pihm->elem[i].geol.depth - pihm->elem[i].ws.fbr_gw);
        CD->Vcele[RT_FBR_UNSAT(i)].vol_o = pihm->elem[i].topo.area * CD->Vcele[RT_FBR_UNSAT(i)].height_o;
        CD->Vcele[RT_FBR_UNSAT(i)].vol = pihm->elem[i].topo.area * pihm->elem[i].geol.depth;
        CD->Vcele[RT_FBR_UNSAT(i)].type = FBR_UNSAT_VOL;

        /* The saturation of unsaturated zone is the Hu divided by height of
         * this cell */
        if (CD->Vcele[RT_FBR_UNSAT(i)].sat > 1.0)
            fprintf(stderr,
                "Fatal Error, FBR Unsaturated Zone Initialization For RT Failed!\n");
#endif
    }

    CD->CalPorosity = pihm->cal.porosity;
    CD->CalRate = pihm->cal.rate;
    CD->CalSSA = pihm->cal.ssa;
    CD->CalPrcpconc = pihm->cal.prcpconc;
    CD->CalInitconc = pihm->cal.initconc;
    CD->CalXsorption = pihm->cal.Xsorption;

    for (i = 0; i < nriver; i++)
    {
        /* Initializing volumetrics for river cells */
        CD->Vcele[RT_RIVER(i)].height_o = pihm->river[i].ws.gw;
        CD->Vcele[RT_RIVER(i)].height_t = pihm->river[i].ws.gw;
        CD->Vcele[RT_RIVER(i)].area = pihm->river[i].topo.area;
        CD->Vcele[RT_RIVER(i)].porosity = 1.0;
        CD->Vcele[RT_RIVER(i)].sat = 1.0;
        CD->Vcele[RT_RIVER(i)].vol_o = pihm->river[i].topo.area * pihm->river[i].ws.gw;
        CD->Vcele[RT_RIVER(i)].vol = pihm->river[i].topo.area * pihm->river[i].ws.gw;
        CD->Vcele[RT_RIVER(i)].type = RIVER_VOL;
    }

    /* Initialize virtual cell */
    CD->Vcele[PRCP_VOL - 1].height_o = 0.0;
    CD->Vcele[PRCP_VOL - 1].height_t = 0.0;
    CD->Vcele[PRCP_VOL - 1].area = 0.0;
    CD->Vcele[PRCP_VOL - 1].porosity = 0.0;
    CD->Vcele[PRCP_VOL - 1].sat = 0.0;
    CD->Vcele[PRCP_VOL - 1].vol_o = 0.0;
    CD->Vcele[PRCP_VOL - 1].vol = 0.0;

    CD->Vcele[VIRTUAL_VOL - 1].height_o = 1.0;
    CD->Vcele[VIRTUAL_VOL - 1].height_t = 1.0;
    CD->Vcele[VIRTUAL_VOL - 1].area = 1.0;
    CD->Vcele[VIRTUAL_VOL - 1].porosity = 1.0;
    CD->Vcele[VIRTUAL_VOL - 1].sat = 1.0;
    CD->Vcele[VIRTUAL_VOL - 1].vol_o = 1.0;
    CD->Vcele[VIRTUAL_VOL - 1].vol = 1.0;

    for (i = 0; i < CD->NumSpc; i++)
    {
        if (strcmp(CD->chemtype[i].ChemName, "pH") == 0)
        {
            strcpy(CD->chemtype[i].ChemName, "H+");
            speciation_flg = 1;
        }
    }

    /* Initializing concentration distributions */
    fprintf(stderr,
        "\n Initializing concentration, Vcele [i, 0 ~ NumVol]... \n");

    for (i = 0; i < CD->NumVol; i++)
    {
        CD->Vcele[i].index = i + 1;
        CD->Vcele[i].t_conc = (double *)calloc(CD->NumStc, sizeof(double));
        CD->Vcele[i].p_conc = (double *)calloc(CD->NumStc, sizeof(double));
        CD->Vcele[i].s_conc = (double *)calloc(CD->NumSsc, sizeof(double));
        CD->Vcele[i].p_actv = (double *)calloc(CD->NumStc, sizeof(double));
        CD->Vcele[i].p_para = (double *)calloc(CD->NumStc, sizeof(double));
        CD->Vcele[i].p_type = (int *)calloc(CD->NumStc, sizeof(int));
        CD->Vcele[i].log10_pconc = (double *)calloc(CD->NumStc, sizeof(double));
        CD->Vcele[i].log10_sconc = (double *)calloc(CD->NumSsc, sizeof(double));
        CD->Vcele[i].btcv_pconc = (double *)calloc(CD->NumStc, sizeof(double));

        CD->Vcele[i].illness = 0;

        for (j = 0; j < CD->NumStc; j++)
        {
            if ((speciation_flg == 1) &&
                (strcmp(CD->chemtype[j].ChemName, "H+") == 0))
            {
                CD->Vcele[i].p_conc[j] = pow(10,
                    -(Condition_vcele[condition_index[i] - 1].t_conc[j]));
                CD->Vcele[i].t_conc[j] = CD->Vcele[i].p_conc[j];
                CD->Vcele[i].p_actv[j] = CD->Vcele[i].p_conc[j];
                CD->Vcele[i].t_conc[j] = CD->Vcele[i].p_conc[j];
                CD->Vcele[i].p_type[j] = 1;
            }
            else if (CD->chemtype[j].itype == MINERAL)
            {
                CD->Vcele[i].t_conc[j] =
                    Condition_vcele[condition_index[i] - 1].t_conc[j];
                CD->Vcele[i].p_conc[j] = CD->Vcele[i].t_conc[j];
                CD->Vcele[i].p_actv[j] = 1.0;
                CD->Vcele[i].p_para[j] =
                    Condition_vcele[condition_index[i] - 1].p_para[j];
                CD->Vcele[i].p_type[j] =
                    Condition_vcele[condition_index[i] - 1].p_type[j];
            }
            else
            {
                if (strcmp(CD->chemtype[j].ChemName, "DOC") == 0)
                {
                    CD->Vcele[i].t_conc[j] = CD->CalInitconc *
                        Condition_vcele[condition_index[i] - 1].t_conc[j];
                }
                else
                {
                    CD->Vcele[i].t_conc[j] =
                        Condition_vcele[condition_index[i] - 1].t_conc[j];
                }
                CD->Vcele[i].p_conc[j] = CD->Vcele[i].t_conc[j] * 0.5;
                CD->Vcele[i].p_actv[j] = CD->Vcele[i].p_conc[j];
                CD->Vcele[i].p_para[j] =
                    Condition_vcele[condition_index[i] - 1].p_para[j];
                CD->Vcele[i].p_type[j] =
                    Condition_vcele[condition_index[i] - 1].p_type[j];
            }
        }
        for (j = 0; j < CD->NumSsc; j++)
        {
            CD->Vcele[i].s_conc[j] = ZERO;
        }
    }

    /*
     * Beginning configuring the connectivity for flux
     */
    for (i = 0; i < nelem; i++)
    {
        total_area += pihm->elem[i].topo.area;
    }

    CD->NumFac = NUM_EDGE * nelem * 2 + 3 * nelem + 6 * nriver;
    CD->NumDis = 2 * 3 * nelem + 3 * nelem;

    fprintf(stderr, "\n Total area of the watershed is %f [m^2]. \n",
        total_area);

    for (i = 0; i < CD->NumPUMP; i++)
    {
        CD->pumps[i].flow_rate = CD->pumps[i].flow_rate;
        fprintf(stderr, "\n PUMP rate is specified %g [m^3/d]. \n",
            CD->pumps[i].flow_rate);
    }

    /* Configuring the lateral connectivity of GW grid blocks */
    fprintf(stderr,
        "\n Configuring the lateral connectivity of GW grid blocks... \n");

    CD->Flux = (face *) malloc(CD->NumFac * sizeof(face));

    for (i = 0; i < nelem; i++)
    {
        int             elemlo;
        int             elemuu;
        int             elemll;
        double          distance;

        for (j = 0; j < 3; j++)
        {
            if (pihm->elem[i].nabr[j] != NO_FLOW)
            {
                elemlo = pihm->elem[i].nabr[j];
                elemuu = (pihm->elem[i].nabr[j] > 0) ?
                    upstream(pihm->elem[i],
                    pihm->elem[pihm->elem[i].nabr[j] - 1], pihm) : 0;
                elemll = (pihm->elem[i].nabr[j] > 0) ?
                    upstream(pihm->elem[pihm->elem[i].nabr[j] - 1],
                    pihm->elem[i], pihm) : 0;
                distance = Dist2Edge(&pihm->meshtbl, &pihm->elem[i], j);

                /* Initialize GW fluxes */
                CD->Flux[RT_LAT_GW(i, j)].nodeup = CD->Vcele[RT_GW(i)].index;
                CD->Flux[RT_LAT_GW(i, j)].node_trib = 0;
                CD->Flux[RT_LAT_GW(i, j)].nodelo = (elemlo > 0) ?
                    CD->Vcele[RT_GW(elemlo - 1)].index :
                    CD->Vcele[RT_RIVER(-elemlo - 1)].index;
                CD->Flux[RT_LAT_GW(i, j)].nodeuu = (elemuu > 0) ?
                    CD->Vcele[RT_GW(elemuu - 1)].index : 0;
                CD->Flux[RT_LAT_GW(i, j)].nodell = (elemll > 0) ?
                    CD->Vcele[RT_GW(elemll - 1)].index : 0;
                CD->Flux[RT_LAT_GW(i, j)].flux_trib = 0.0;
                CD->Flux[RT_LAT_GW(i, j)].BC = DISPERSION;
                CD->Flux[RT_LAT_GW(i, j)].distance = distance;

                /* Initialize unsat zone fluxes */
                CD->Flux[RT_LAT_UNSAT(i, j)].nodeup = CD->Vcele[RT_UNSAT(i)].index;
                CD->Flux[RT_LAT_UNSAT(i, j)].node_trib = 0;
                CD->Flux[RT_LAT_UNSAT(i, j)].nodelo = (elemlo > 0) ?
                    CD->Vcele[RT_UNSAT(elemlo - 1)].index :
                    CD->Vcele[RT_RIVER(-elemlo - 1)].index;
                CD->Flux[RT_LAT_UNSAT(i, j)].nodeuu = (elemuu > 0) ?
                    CD->Vcele[RT_UNSAT(elemuu - 1)].index :0;
                CD->Flux[RT_LAT_UNSAT(i, j)].nodell = (elemll > 0) ?
                    CD->Vcele[RT_UNSAT(elemll - 1)].index : 0;
                CD->Flux[RT_LAT_UNSAT(i, j)].flux_trib = 0.0;
                CD->Flux[RT_LAT_UNSAT(i, j)].BC = DISPERSION;
                CD->Flux[RT_LAT_UNSAT(i, j)].distance = distance;
            }
            else
            {
                CD->Flux[RT_LAT_GW(i, j)].nodeup = CD->Vcele[RT_GW(i)].index;
                CD->Flux[RT_LAT_GW(i, j)].node_trib = 0;
                CD->Flux[RT_LAT_GW(i, j)].nodelo = 0;
                CD->Flux[RT_LAT_GW(i, j)].nodeuu = 0;
                CD->Flux[RT_LAT_GW(i, j)].nodell = 0;
                CD->Flux[RT_LAT_GW(i, j)].flux_trib = 0.0;
                CD->Flux[RT_LAT_GW(i, j)].BC = NO_FLOW;
                CD->Flux[RT_LAT_GW(i, j)].distance = 0.0;

                CD->Flux[RT_LAT_UNSAT(i, j)].nodeup = CD->Vcele[RT_UNSAT(i)].index;;
                CD->Flux[RT_LAT_UNSAT(i, j)].node_trib = 0;
                CD->Flux[RT_LAT_UNSAT(i, j)].nodelo = 0;
                CD->Flux[RT_LAT_UNSAT(i, j)].nodeuu = 0;
                CD->Flux[RT_LAT_UNSAT(i, j)].nodell = 0;
                CD->Flux[RT_LAT_UNSAT(i, j)].flux_trib = 0.0;
                CD->Flux[RT_LAT_UNSAT(i, j)].BC = NO_FLOW;
                CD->Flux[RT_LAT_UNSAT(i, j)].distance = 0.0;
            }
        }

        /* Infiltration */
        CD->Flux[RT_INFIL(i)].nodeup = CD->Vcele[RT_UNSAT(i)].index;
        CD->Flux[RT_INFIL(i)].node_trib = 0;
        CD->Flux[RT_INFIL(i)].nodelo = PRCP_VOL;
        CD->Flux[RT_INFIL(i)].nodeuu = 0;
        CD->Flux[RT_INFIL(i)].nodell = 0;
        CD->Flux[RT_INFIL(i)].flux_trib = 0.0;
        CD->Flux[RT_INFIL(i)].BC = NO_DISP;
        CD->Flux[RT_INFIL(i)].distance = 0.0;

        /* Rechage centered at unsat blocks */
        CD->Flux[RT_RECHG_UNSAT(i)].nodeup = CD->Vcele[RT_UNSAT(i)].index;
        CD->Flux[RT_RECHG_UNSAT(i)].node_trib = 0;
        CD->Flux[RT_RECHG_UNSAT(i)].nodelo = CD->Vcele[RT_GW(i)].index;
        CD->Flux[RT_RECHG_UNSAT(i)].nodeuu = 0;
        CD->Flux[RT_RECHG_UNSAT(i)].nodell = 0;
        CD->Flux[RT_RECHG_UNSAT(i)].flux_trib = 0.0;
        CD->Flux[RT_RECHG_UNSAT(i)].BC = DISPERSION;
        CD->Flux[RT_RECHG_UNSAT(i)].distance = 0.1 * pihm->elem[i].soil.depth;

        /* Recharge centered at gw blocks */
        CD->Flux[RT_RECHG_GW(i)].nodeup = CD->Vcele[RT_GW(i)].index;
        CD->Flux[RT_RECHG_GW(i)].node_trib = 0;
        CD->Flux[RT_RECHG_GW(i)].nodelo = CD->Vcele[RT_UNSAT(i)].index;
        CD->Flux[RT_RECHG_GW(i)].nodeuu = 0;
        CD->Flux[RT_RECHG_GW(i)].nodell = 0;
        CD->Flux[RT_RECHG_GW(i)].flux_trib = 0.0;
        CD->Flux[RT_RECHG_GW(i)].BC = DISPERSION;
        CD->Flux[RT_RECHG_GW(i)].distance = 0.1 * pihm->elem[i].soil.depth;
    }

    /* Configuring the vertical connectivity of UNSAT - GW blocks */
    fprintf(stderr,
        "\n Configuring the vertical connectivity of UNSAT - GW grid blocks... \n");

    /* Configuring the connectivity of RIVER and EBR blocks */
    fprintf(stderr,
        "\n Configuring the connectivity of RIVER & EBR grid blocks... \n");

    for (i = 0; i < nriver; i++)
    {
        /* Between River and Left */
        /* River to left OFL 2 */
        CD->Flux[RT_LEFT_SURF2RIVER(i)].nodeup = CD->Vcele[RT_RIVER(i)].index;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].node_trib = 0;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].nodelo = VIRTUAL_VOL;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].nodeuu = 0;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].nodell = 0;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].BC = NO_DISP;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].flux = 0.0;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].flux_trib = 0.0;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].distance = 1.0;
        CD->Flux[RT_LEFT_SURF2RIVER(i)].s_area = 0.0;

        /* Between River and Right */
        /* River to right OFL 3 */
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].nodeup = CD->Vcele[RT_RIVER(i)].index;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].node_trib = 0;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].nodelo = VIRTUAL_VOL;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].nodeuu = 0;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].nodell = 0;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].BC = NO_DISP;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].flux = 0.0;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].flux_trib = 0.0;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].distance = 1.0;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].s_area = 0.0;

        /* Between Left and EBR */
        /* EBR to left  7 + 4 */
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].nodeup = CD->Vcele[RT_RIVER(i)].index;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].node_trib = 0;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].nodelo =
            CD->Vcele[RT_GW(pihm->river[i].leftele - 1)].index;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].nodeuu = 0;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].nodell = 0;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].BC = DISPERSION;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].flux = 0.0;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].flux_trib = 0.0;
        for (j = 0; j < NUM_EDGE; j++)
        {
            if (-pihm->elem[pihm->river[i].leftele - 1].nabr[j] == i + 1)
            {
                CD->Flux[RT_LEFT_AQIF2RIVER(i)].distance =
                CD->Flux[RT_LAT_GW(pihm->river[i].leftele - 1, j)].distance;
                break;
            }
        }
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].s_area = pihm->river[i].shp.length *
            pihm->elem[pihm->river[i].leftele - 1].soil.depth;

        /* Between Right and EBR */
        /* EBR to right 8 + 5 */
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].nodeup = CD->Vcele[RT_RIVER(i)].index;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].node_trib = 0;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].nodelo =
            CD->Vcele[RT_GW(pihm->river[i].rightele - 1)].index;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].nodeuu = 0;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].nodell = 0;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].BC = DISPERSION;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].flux = 0.0;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].flux_trib = 0.0;
        for (j = 0; j < NUM_EDGE; j++)
        {
            if (-pihm->elem[pihm->river[i].rightele - 1].nabr[j] == i + 1)
            {
                CD->Flux[RT_RIGHT_AQIF2RIVER(i)].distance =
                CD->Flux[RT_LAT_GW(pihm->river[i].rightele - 1, j)].distance;
                break;
            }
        }
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].s_area =
            pihm->river[i].shp.length *
            pihm->elem[pihm->river[i].rightele - 1].soil.depth;

        /* Between EBR */
        /* To downstream EBR 9 */
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].nodeup = CD->Vcele[RT_RIVER(i)].index;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].nodelo = (pihm->river[i].down < 0) ?
            VIRTUAL_VOL : CD->Vcele[RT_RIVER(pihm->river[i].down - 1)].index;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].node_trib = 0;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].nodeuu = 0;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].nodell = 0;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].BC = NO_DISP;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].flux = 0.0;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].flux_trib = 0.0;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].distance = 1.0;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].s_area = 0.0;

        /* From upstream EBR 10 */
        CD->Flux[RT_UP_RIVER2RIVER(i)].nodeup = CD->Vcele[RT_RIVER(i)].index;
        CD->Flux[RT_UP_RIVER2RIVER(i)].nodelo = (pihm->river[i].up[0] < 0) ?
            VIRTUAL_VOL : CD->Vcele[RT_RIVER(pihm->river[i].up[0] - 1)].index;
        CD->Flux[RT_UP_RIVER2RIVER(i)].node_trib = (pihm->river[i].up[1] < 0) ?
            pihm->river[i].up[1] :
            CD->Vcele[RT_RIVER(pihm->river[i].up[1] - 1)].index;
        CD->Flux[RT_UP_RIVER2RIVER(i)].nodeuu = 0;
        CD->Flux[RT_UP_RIVER2RIVER(i)].nodell = 0;
        CD->Flux[RT_UP_RIVER2RIVER(i)].BC = NO_DISP;
        CD->Flux[RT_UP_RIVER2RIVER(i)].flux = 0.0;
        CD->Flux[RT_UP_RIVER2RIVER(i)].flux_trib = 0.0;
        CD->Flux[RT_UP_RIVER2RIVER(i)].distance = 1.0;
        CD->Flux[RT_UP_RIVER2RIVER(i)].s_area = 0.0;
    }

    for (k = 0; k < CD->NumFac; k++)
    {
        CD->Flux[k].velocity = 0.0;
        CD->Flux[k].flux = 0.0; /* Initialize 0.0 for above sections of GW-GW,
                                 * UNSAT-UNSAT, GW-UNSAT, UNSAT-GW */
        CD->Flux[k].flux_trib = 0.0;
        CD->Flux[k].s_area = 0.0;
    }

    CD->SPCFlg = speciation_flg;
    Lookup(database, CD);
    /* Update the concentration of mineral after get the molar volume of
     * mineral */

    double          Cal_PCO2 = 1.0;
    double          Cal_Keq = 1.0;
    for (i = 0; i < CD->NumAkr + CD->NumMkr; i++)
    {
        if (!strcmp(CD->chemtype[i + CD->NumSpc + CD->NumAds +
            CD->NumCex].ChemName, "'CO2(*g)'"))
        {
            CD->KeqKinect[i] += log10(Cal_PCO2);
        }
        else
        {
            CD->KeqKinect[i] += log10(Cal_Keq);
        }
    }

    fprintf(stderr, "\n Kinetic Mass Matrx (calibrated Keq)! \n");
    fprintf(stderr, "%-15s", " ");
    for (i = 0; i < CD->NumStc; i++)
        fprintf(stderr, "%-14s", CD->chemtype[i].ChemName);
    fprintf(stderr, "\n");
    for (j = 0; j < CD->NumMkr + CD->NumAkr; j++)
    {
        fprintf(stderr, " %-14s",
            CD->chemtype[j + CD->NumSpc + CD->NumAds + CD->NumCex].ChemName);
        for (i = 0; i < CD->NumStc; i++)
        {
            fprintf(stderr, "%-14.2f", CD->Dep_kinetic[j][i]);
        }
        fprintf(stderr, " Keq = %-6.2f\n", CD->KeqKinect[j]);
    }
    fprintf(stderr, "\n");
    /* Use calibration coefficient to produce new Keq values for
     * 1) CO2, 2) other kinetic reaction */

    fprintf(stderr,
        " \n Mass action species type determination (0: immobile, 1: mobile, 2: Mixed) \n");
    for (i = 0; i < CD->NumSpc; i++)
    {
        if (CD->chemtype[i].itype == AQUEOUS)
            CD->chemtype[i].mtype = 1;
        else
            CD->chemtype[i].mtype = 0;
        for (j = 0; j < CD->NumStc + CD->NumSsc; j++)
        {
            if ((CD->Totalconc[i][j] != 0) &&
                (CD->chemtype[j].itype != CD->chemtype[i].mtype))
                CD->chemtype[i].mtype = 2;
        }
        /*
         * if (strcmp( CD->chemtype[i].ChemName, "'H+'") == 0)
         * CD->chemtype[i].mtype = 1;
         */
        fprintf(stderr, " %12s\t%10d\n", CD->chemtype[i].ChemName,
            CD->chemtype[i].mtype);
    }

    fprintf(stderr,
        " \n Individual species type determination (1: aqueous, 2: adsorption, 3: ion exchange, 4: solid) \n");
    for (i = 0; i < CD->NumStc + CD->NumSsc; i++)
    {
        fprintf(stderr, " %12s\t%10d\n", CD->chemtype[i].ChemName,
            CD->chemtype[i].itype);
    }

    for (i = 0; i < CD->NumVol; i++)
    {
        for (j = 0; j < CD->NumStc; j++)
        {
            if (CD->chemtype[j].itype == MINERAL)
            {
                if (CD->RelMin == 0)
                {
                    /* Absolute mineral volume fraction */
                    CD->Vcele[i].t_conc[j] =
                        CD->Vcele[i].t_conc[j] * 1000 /
                        CD->chemtype[j].MolarVolume / CD->Vcele[i].porosity;
                    CD->Vcele[i].p_conc[j] = CD->Vcele[i].t_conc[j];
                }
                if (CD->RelMin == 1)
                {
                    /* Relative mineral volume fraction */
                    /* Porosity can be 1.0 so the relative fraction option needs
                     * a small modification */
                    CD->Vcele[i].t_conc[j] = CD->Vcele[i].t_conc[j] *
                        (1 - CD->Vcele[i].porosity + INFTYSMALL) * 1000 /
                        CD->chemtype[j].MolarVolume / CD->Vcele[i].porosity;
                    CD->Vcele[i].p_conc[j] = CD->Vcele[i].t_conc[j];
                }
            }
            if ((CD->chemtype[j].itype == CATION_ECHG) ||
                (CD->chemtype[j].itype == ADSORPTION))
            {
                /* Change the unit of CEC (eq/g) into C(ion site)
                 * (eq/L porous space), assuming density of solid is always
                 * 2650 g/L solid */
                CD->Vcele[i].t_conc[j] =
                    CD->Vcele[i].t_conc[j] * (1 - CD->Vcele[i].porosity) * 2650;
                CD->Vcele[i].p_conc[j] = CD->Vcele[i].t_conc[j];
            }
        }
    }

    CD->SPCFlg = 1;
    if (!CD->RecFlg)
    {
        for (i = 0; i < nelem; i++)
        {
            Speciation(CD, RT_GW(i));
        }
    }
    CD->SPCFlg = 0;

    /* Initialize unsaturated zone concentrations to be the same as in saturated
     * zone */
    for (i = 0; i < nelem; i++)
    {
        for (k = 0; k < CD->NumStc; k++)
        {
            CD->Vcele[RT_UNSAT(i)].t_conc[k] = CD->Vcele[RT_GW(i)].t_conc[k];
            CD->Vcele[RT_UNSAT(i)].p_conc[k] = CD->Vcele[RT_GW(i)].p_conc[k];
            CD->Vcele[RT_UNSAT(i)].p_actv[k] = CD->Vcele[RT_GW(i)].p_actv[k];
        }
    }

    /* Initialize river concentrations */
    for (i = 0; i < nriver; i++)
    {
        for (k = 0; k < CD->NumStc; k++)
        {
            if (CD->chemtype[k].itype != AQUEOUS)
            {
                CD->Vcele[RT_RIVER(i)].t_conc[k] = 1.0E-20;
                CD->Vcele[RT_RIVER(i)].p_conc[k] = 1.0E-20;
                CD->Vcele[RT_RIVER(i)].p_actv[k] = 1.0E-20;
            }
        }
    }

    for (i = 0; i < num_conditions; i++)
    {
        free(Condition_vcele[i].t_conc);
        free(Condition_vcele[i].p_conc);
        free(Condition_vcele[i].p_para);
        free(Condition_vcele[i].p_type);
    }
    free(Condition_vcele);

    for (i = 0; i < num_conditions; i++)
        free(chemcon[i]);
    free(chemcon);

    for (i = 0; i < num_conditions + 1; i++)
    {
        for (j = 0; j < CD->NumStc; j++)
            free(con_chem_name[i][j]);
        free(con_chem_name[i]);
    }
    free(con_chem_name);

    free(chemfn);
    free(datafn);
    free(forcfn);
    free(condition_index);

    free(Global_type.ChemName);
    for (i = 0; i < words_line; i++)
    {
        free(tmpstr[i]);
    }
    free(tmpstr);

    fclose(chemfile);
    fclose(database);
    fclose(prepconc);
}

void fluxtrans(int t, int stepsize, const pihm_struct pihm, Chem_Data CD,
    double *t_duration_transp, double *t_duration_react)
{
    /* unit of t and stepsize: min
     * swi irreducible water saturation
     * hn  non mobile water height
     * ht  transient zone height
     */
    int             i, k = 0;
    double          rt_step, invavg, unit_c;

    unit_c = stepsize / UNIT_C;
    int             VIRTUAL_VOL = CD->NumVol;
    int             PRCP_VOL = CD->NumVol - 1;

#if defined(_OPENMP)
# pragma omp parallel for
#endif
    for (i = 0; i < nelem; i++)
    {
        int             j;

        for (j = 0; j < 3; j++)
        {
            if (pihm->elem[i].nabr[j] != NO_FLOW)
            {
                /* Flux for GW lateral flow */
                CD->Flux[RT_LAT_GW(i, j)].flux += 1.0 * pihm->elem[i].wf.subsurf[j] * 86400;  /* Test lateral dilution */

                /* Flux for UNSAT lateral flow */
                CD->Flux[RT_LAT_UNSAT(i, j)].s_area = 1.0;
            }
        }


        /* Flux for UNSAT - GW vertical flow */
        CD->Flux[RT_RECHG_UNSAT(i)].flux += (pihm->elem[i].wf.rechg * 86400) *
            CD->Vcele[RT_UNSAT(i)].area;

        CD->Flux[RT_RECHG_GW(i)].flux += (-pihm->elem[i].wf.rechg * 86400) *
            CD->Vcele[RT_GW(i)].area;
    }

    /* Flux for RIVER flow */
    for (i = 0; i < nriver; i++)
    {
        if (pihm->river[i].down < 0)
        {
            CD->riv += pihm->river[i].wf.rivflow[1] * 86400;
        }
    }

    if ((t - pihm->ctrl.starttime / 60) % 1440 == 0)
    {
        CD->rivd = CD->riv / 1440;  /* Averaging the sum of 1440 mins for a
                                     * daily discharge, rivFlx1 */
        CD->riv = 0;
    }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
    for (i = 0; i < nriver; i++)
    {
        CD->Flux[RT_LEFT_SURF2RIVER(i)].flux += pihm->river[i].wf.rivflow[2] * 86400;
        CD->Flux[RT_RIGHT_SURF2RIVER(i)].flux += pihm->river[i].wf.rivflow[3] * 86400;
        CD->Flux[RT_LEFT_AQIF2RIVER(i)].flux += pihm->river[i].wf.rivflow[7] * 86400 +
            pihm->river[i].wf.rivflow[4] * 86400;
        CD->Flux[RT_RIGHT_AQIF2RIVER(i)].flux += pihm->river[i].wf.rivflow[8] * 86400 +
            pihm->river[i].wf.rivflow[5] * 86400;
        CD->Flux[RT_DOWN_RIVER2RIVER(i)].flux += pihm->river[i].wf.rivflow[9] * 86400 +
            pihm->river[i].wf.rivflow[1] * 86400;
        CD->Flux[RT_UP_RIVER2RIVER(i)].flux += pihm->river[i].wf.rivflow[10] * 86400 +
            pihm->river[i].wf.rivflow[0] * 86400;

        if (CD->Flux[RT_UP_RIVER2RIVER(i)].node_trib > 0)
        {
            CD->Flux[RT_UP_RIVER2RIVER(i)].flux_trib +=
                -(pihm->river[pihm->river[i].up[1] - 1].wf.rivflow[9] * 86400 +
                pihm->river[pihm->river[i].up[1] - 1].wf.rivflow[1] * 86400);
        }
    }

    /*
     * Update the cell volumetrics every averaging cycle
     */
    if (t - pihm->ctrl.starttime / 60 - (int)CD->TimLst == CD->AvgScl * stepsize)
    {
        /* Update the concentration in precipitation here. */
        if (CD->PrpFlg == 2)
        {
            IntrplForc(&CD->TSD_prepconc[0], t * 60, CD->TSD_prepconc[0].nspec,
                NO_INTRPL);

#if defined(_OPENMP)
# pragma omp parallel for
#endif
            for (i = 0; i < CD->TSD_prepconc[0].nspec; i++)
            {
                if (CD->prepconcindex[i] > 0)
                {
                    int             ind;

                    ind = CD->prepconcindex[i] - 1;
                    if (CD->Precipitation.t_conc[ind] !=
                        CD->TSD_prepconc[0].value[i])
                    {
                        CD->Precipitation.t_conc[ind] =
                            CD->TSD_prepconc[0].value[i];
                        fprintf(stderr,
                            "  %s in precipitation is changed to %6.4g\n",
                            CD->chemtype[ind].ChemName,
                            CD->Precipitation.t_conc[ind]);
                    }
                }
            }
        }

#ifdef _OPENMP
# pragma omp parallel for
#endif
        for (i = 0; i < nelem; i++)
        {
            CD->Vcele[RT_GW(i)].height_o = CD->Vcele[RT_GW(i)].height_t;
            CD->Vcele[RT_GW(i)].height_t = MAX(pihm->elem[i].ws.gw, 1.0E-5);
            CD->Vcele[RT_GW(i)].height_int = CD->Vcele[RT_GW(i)].height_t;
            CD->Vcele[RT_GW(i)].height_sp =
                (CD->Vcele[RT_GW(i)].height_t - CD->Vcele[RT_GW(i)].height_o) * invavg;
            CD->Vcele[RT_GW(i)].vol_o =
                CD->Vcele[RT_GW(i)].area * CD->Vcele[RT_GW(i)].height_o;
            CD->Vcele[RT_GW(i)].vol =
                CD->Vcele[RT_GW(i)].area * CD->Vcele[RT_GW(i)].height_t;

            /* Update the unsaturated zone (vadoze) */
            CD->Vcele[RT_UNSAT(i)].height_o = CD->Vcele[RT_UNSAT(i)].height_t;
            CD->Vcele[RT_UNSAT(i)].height_t =
                MAX(((pihm->elem[i].ws.unsat * (pihm->elem[i].soil.smcmax -
                pihm->elem[i].soil.smcmin) +
                (pihm->elem[i].soil.depth - CD->Vcele[RT_GW(i)].height_t) *
                pihm->elem[i].soil.smcmin) / pihm->elem[i].soil.smcmax),
                1.0E-5);
            CD->Vcele[RT_UNSAT(i)].height_int = CD->Vcele[RT_UNSAT(i)].height_t;
            CD->Vcele[RT_UNSAT(i)].height_sp =
                (CD->Vcele[RT_UNSAT(i)].height_t - CD->Vcele[RT_UNSAT(i)].height_o) * invavg;
            CD->Vcele[RT_UNSAT(i)].vol_o = CD->Vcele[RT_UNSAT(i)].area * CD->Vcele[RT_UNSAT(i)].height_o;
            CD->Vcele[RT_UNSAT(i)].vol = CD->Vcele[RT_UNSAT(i)].area * CD->Vcele[RT_UNSAT(i)].height_t;
            CD->Vcele[RT_UNSAT(i)].sat = CD->Vcele[RT_UNSAT(i)].height_t /
                (pihm->elem[i].soil.depth - CD->Vcele[RT_GW(i)].height_t);
        }

#ifdef _OPENMP
#pragma omp parallel for
#endif
        /* Update river cells */
        for (i = 0; i < nriver; i++)
        {
            CD->Vcele[RT_RIVER(i)].height_o = CD->Vcele[RT_RIVER(i)].height_t;
            CD->Vcele[RT_RIVER(i)].height_t = MAX(pihm->river[i].ws.gw, 1.0E-5) +
                MAX(pihm->river[i].ws.stage, 1.0E-5) / CD->Vcele[RT_RIVER(i)].porosity;
            CD->Vcele[RT_RIVER(i)].height_int = CD->Vcele[RT_RIVER(i)].height_t;
            CD->Vcele[RT_RIVER(i)].height_sp =
                (CD->Vcele[RT_RIVER(i)].height_t - CD->Vcele[RT_RIVER(i)].height_o) * invavg;
            CD->Vcele[RT_RIVER(i)].area = pihm->river[i].topo.area;
            CD->Vcele[RT_RIVER(i)].vol_o = CD->Vcele[RT_RIVER(i)].area * CD->Vcele[RT_RIVER(i)].height_o;
            CD->Vcele[RT_RIVER(i)].vol = CD->Vcele[RT_RIVER(i)].area * CD->Vcele[RT_RIVER(i)].height_t;
        }

        invavg = stepsize / (double)CD->AvgScl;

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (k = 0; k < CD->NumFac; k++)
        {
            CD->Flux[k].flux *= invavg;
        }

        /*
         * Correct recharge and infiltration to converve mass balance
         */
        Monitor(stepsize * (double)CD->AvgScl, pihm, CD);

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (i = 0; i < nelem; i++)
        {
            int             j;

            /* For gw cells, contact area is needed for dispersion; */
            for (j = 0; j < 3; j++)
            {
                double              h1, h2;
                double              area;

                if (CD->Flux[RT_LAT_GW(i, j)].BC == NO_FLOW)
                {
                    continue;
                }

                if (pihm->elem[i].nabr[j] > 0)
                {
                    h1 = 0.5 *
                        (CD->Vcele[RT_GW(i)].height_o +
                        CD->Vcele[RT_GW(i)].height_t);
                    h2 = 0.5 *
                        (CD->Vcele[RT_GW(pihm->elem[i].nabr[j] - 1)].height_o +
                        CD->Vcele[RT_GW(pihm->elem[i].nabr[j] - 1)].height_t);

                    CD->Flux[RT_LAT_GW(i, j)].s_area =
                        (CD->Flux[RT_LAT_GW(i, j)].flux > 0.0) ?
                        pihm->elem[i].topo.edge[j] * h1 :
                        pihm->elem[i].topo.edge[j] * h2;
                }
                else if (pihm->elem[i].nabr[j] < 0)
                {
                    h1 = 0.5 *
                        (CD->Vcele[RT_GW(i)].height_o +
                        CD->Vcele[RT_GW(i)].height_t);
                    h2 = 0.5 *
                        (CD->Vcele[RT_RIVER(-pihm->elem[i].nabr[j] - 1)].height_o +
                        CD->Vcele[RT_RIVER(-pihm->elem[i].nabr[j] - 1)].height_t);

                    CD->Flux[RT_LAT_GW(i, j)].s_area =
                        (CD->Flux[RT_LAT_GW(i, j)].flux > 0.0) ?
                        pihm->elem[i].topo.edge[j] * h1 :
                        pihm->elem[i].topo.edge[j] * h2;
                }

                /* Calculate velocity according to flux and area */
                CD->Flux[RT_LAT_GW(i, j)].velocity =
                    (CD->Flux[RT_LAT_GW(i, j)].s_area > 1.0E-4) ?
                    CD->Flux[RT_LAT_GW(i, j)].flux /
                    CD->Flux[RT_LAT_GW(i, j)].s_area :
                    1.0E-10;
            }

            CD->Flux[RT_RECHG_UNSAT(i)].s_area = pihm->elem[i].topo.area;
            CD->Flux[RT_RECHG_UNSAT(i)].velocity =
                CD->Flux[RT_RECHG_UNSAT(i)].flux / pihm->elem[i].topo.area;

            CD->Flux[RT_RECHG_GW(i)].s_area = pihm->elem[i].topo.area;
            CD->Flux[RT_RECHG_GW(i)].velocity =
                CD->Flux[RT_RECHG_GW(i)].flux / pihm->elem[i].topo.area;
        }

        /* Correct river flux area and velocity */
#ifdef _OPENMP
# pragma omp parallel for
#endif
        for (i = 0; i < nriver; i++)
        {
            int             j;

            for (j = 0; j < NUM_EDGE; j++)
            {
                if (-pihm->elem[pihm->river[i].leftele - 1].nabr[j] == i + 1)
                {
                    CD->Flux[RT_LEFT_AQIF2RIVER(i)].s_area =
                    CD->Flux[RT_LAT_GW(pihm->river[i].leftele - 1, j)].s_area;
                    CD->Flux[RT_LEFT_AQIF2RIVER(i)].velocity =
                    -CD->Flux[RT_LAT_GW(pihm->river[i].leftele - 1, j)].velocity;
                    break;
                }
            }

            for (j = 0; j < NUM_EDGE; j++)
            {
                if (-pihm->elem[pihm->river[i].rightele - 1].nabr[j] == i + 1)
                {
                    CD->Flux[RT_RIGHT_AQIF2RIVER(i)].s_area =
                    CD->Flux[RT_LAT_GW(pihm->river[i].rightele - 1, j)].s_area;
                    CD->Flux[RT_RIGHT_AQIF2RIVER(i)].velocity =
                    -CD->Flux[RT_LAT_GW(pihm->river[i].rightele - 1, j)].velocity;
                    break;
                }
            }
        }

        /* Update virtual volume */

        if (CD->PrpFlg)
        {
#if defined(_OPENMP)
# pragma omp parallel for
#endif
            for (k = 0; k < CD->NumSpc; k++)
            {
                CD->Vcele[PRCP_VOL - 1].t_conc[k] =
                    (strcmp(CD->chemtype[k].ChemName, "'DOC'") == 0) ?
                    CD->Precipitation.t_conc[k] * CD->Condensation *
                    CD->CalPrcpconc :
                    CD->Precipitation.t_conc[k] * CD->Condensation;
            }
        }
        else
        {
#if defined(_OPENMP)
# pragma omp parallel for
#endif
            for (k = 0; k < CD->NumSpc; k++)
            {
                CD->Vcele[PRCP_VOL - 1].t_conc[k] = 0.0;
            }
        }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (k = 0; k < CD->NumStc; k++)
        {
            CD->Vcele[VIRTUAL_VOL - 1].t_conc[k] =
                CD->Precipitation.t_conc[k] * CD->Condensation;
            CD->Vcele[VIRTUAL_VOL - 1].p_conc[k] =
                CD->Precipitation.t_conc[k] * CD->Condensation;
        }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (i = 0; i < CD->NumVol; i++)
        {
            CD->Vcele[i].rt_step = 0.0;
        }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (i = 0; i < CD->NumDis; i++)
        {
            int             j;
            double          peclet;
            if (CD->Flux[i].BC != NO_DISP)
            {
                for (j = 0; j < CD->NumSpc; j++)
                {
                    peclet = fabs(CD->Flux[i].velocity * CD->Flux[i].distance /
                        (CD->chemtype[j].DiffCoe +
                        CD->chemtype[j].DispCoe * CD->Flux[i].velocity));
                    peclet = MAX(peclet, 1.0E-8);
                }

                CD->Vcele[CD->Flux[i].nodeup - 1].rt_step +=
                    fabs(CD->Flux[i].flux / CD->Vcele[CD->Flux[i].nodeup - 1].vol) *
                    (1 + peclet) / peclet;
            }
        }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (i = 0; i < CD->NumOsv; i++)
        {
            CD->Vcele[i].rt_step = 0.6 * UNIT_C / CD->Vcele[i].rt_step;
            CD->Vcele[i].rt_step =
                (CD->Vcele[i].rt_step >= (double)CD->AvgScl) ?
                (double)CD->AvgScl : CD->Vcele[i].rt_step;
        }

        /*
         * RT step control begins
         */
        if (CD->TimLst >= CD->Delay)
        {
            rt_step = stepsize * (double)CD->AvgScl;

            AdptTime(CD, CD->TimLst, rt_step, stepsize * (double)CD->AvgScl,
                t_duration_transp, t_duration_react);

#if defined(_OPENMP)
# pragma omp parallel for
#endif
            for (i = 0; i < CD->NumEle; i++)
            {
                int             j;

                for (j = 0; j < CD->NumStc; j++)
                {
                    if (CD->chemtype[j].itype == MINERAL)
                    {
                        /* Averaging mineral concentration to ensure mass
                         * conservation !! */
                        CD->Vcele[RT_GW(i)].t_conc[j] =
                            (CD->Vcele[RT_GW(i)].t_conc[j] *
                            CD->Vcele[RT_GW(i)].height_t +
                            CD->Vcele[RT_UNSAT(i)].t_conc[j] *
                            (pihm->elem[i].soil.depth -
                            CD->Vcele[RT_GW(i)].height_t)) /
                            pihm->elem[i].soil.depth;
                        CD->Vcele[RT_UNSAT(i)].t_conc[j] =
                            CD->Vcele[RT_GW(i)].t_conc[j];
                        CD->Vcele[RT_GW(i)].p_conc[j] =
                            CD->Vcele[RT_GW(i)].t_conc[j];
                        CD->Vcele[RT_UNSAT(i)].p_conc[j] =
                            CD->Vcele[RT_GW(i)].t_conc[j];
                    }
                }
            }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
            for (i = 0; i < CD->NumOsv; i++)
            {
                int             j;

                /* Make sure intrapolation worked well */
                if (fabs(CD->Vcele[i].height_t - CD->Vcele[i].height_int) >
                    1.0E-6)
                    fprintf(stderr, "%d %6.4f\t%6.4f\n", i,
                        CD->Vcele[i].height_t, CD->Vcele[i].height_int);
                assert(fabs(CD->Vcele[i].height_t - CD->Vcele[i].height_int) <
                    1.0E-6);
                if (CD->Vcele[i].illness >= 20)
                {
                    for (j = 0; j < CD->NumStc; j++)
                        CD->Vcele[i].t_conc[j] = 1.0E-10;
                    fprintf(stderr,
                        " Cell %d isolated due to proneness to err!\n",
                        CD->Vcele[i].index);
                }
            }
        } /* RT step control ends */

        CD->TimLst = t - pihm->ctrl.starttime / 60;

        /* Reset fluxes for next averaging stage */
#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (k = 0; k < CD->NumDis; k++)
        {
            CD->Flux[k].velocity = 0.0;
            CD->Flux[k].flux = 0.0;
            CD->Flux[k].flux_trib = 0.0;
            /* For riv cells, contact area is not needed */
            CD->Flux[k].s_area = 0.0;
        }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (k = 0; k < CD->NumFac; k++)
        {
            CD->Flux[k].flux = 0.0;
            CD->Flux[k].flux_trib = 0.0;
            CD->Flux[k].velocity = 0.0;
        }

        if ((t - pihm->ctrl.starttime / 60) % 60 == 0)
        {
            CD->SPCFlg = 0;

            if (!CD->RecFlg)
            {
#if defined(_OPENMP)
# pragma omp parallel for
#endif
                for (i = 0; i < CD->NumStc; i++)
                {
                    int             j;

                    for (j = 0; j < nriver; j++)
                    {
                        CD->Vcele[RT_RIVER(j)].p_conc[i] =
                            (CD->chemtype[i].itype == MINERAL) ?
                            CD->Vcele[RT_RIVER(j)].t_conc[i] :
                            fabs(CD->Vcele[RT_RIVER(j)].t_conc[i] * 0.1);
                    }
                }
            }

            if (!CD->RecFlg)
            {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (i = 0; i < nriver; i++)
                {
                    Speciation(CD, RT_RIVER(i));
                }
            }
            else
            {
#ifdef _OPENMP
#pragma omp parallel for
#endif
                for (i = 0; i < CD->NumOsv; i++)
                    Speciation(CD, i);
            }
        }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (i = 0; i < CD->NumVol; i++)
        {
            int             j;

            for (j = 0; j < CD->NumStc; j++)
            {
                CD->Vcele[i].log10_pconc[j] = log10(CD->Vcele[i].p_conc[j]);
            }
            for (j = 0; j < CD->NumSsc; j++)
            {
                CD->Vcele[i].log10_sconc[j] = log10(CD->Vcele[i].s_conc[j]);
            }
        }

        for (k = 0; k < CD->NumBTC; k++)
        {
            int             j;

            for (j = 0; j < CD->NumStc; j++)
            {
                if ((CD->BTC_loc[k] >= CD->pumps[0].Pump_Location - 1) &&
                    (j == CD->pumps[0].Position_Species))
                {
                    CD->Vcele[CD->BTC_loc[k]].btcv_pconc[j] =
                        log10((CD->Vcele[CD->BTC_loc[k]].p_conc[j] * CD->rivd +
                        CD->pumps[0].Injection_conc * CD->pumps[0].flow_rate) /
                        (CD->rivd + CD->pumps[0].flow_rate));
                }
                else
                {
                    CD->Vcele[CD->BTC_loc[k]].btcv_pconc[j] =
                        CD->Vcele[CD->BTC_loc[k]].log10_pconc[j];
                }
            }
        }
    }
}

void AdptTime(Chem_Data CD, realtype timelps, double rt_step, double hydro_step,
    double *t_duration_transp, double *t_duration_react)
{
    double          stepsize, end_time;
    int             i, k, int_flg;
    time_t          t_start_transp, t_end_transp;

    stepsize = rt_step;
    end_time = timelps + hydro_step;

    t_start_transp = time(NULL);

    /* Simple check to determine whether or not to intrapolate the gw height */
    if (rt_step >= hydro_step)
    {
        int_flg = 0;
    }
    else
    {
        int_flg = 1;
        fprintf(stderr, " Sub time step intrapolation performed. \n");
    }

    while (timelps < end_time)
    {
        time_t          t_start_react, t_end_react;

        stepsize = (stepsize > end_time - timelps) ?
            end_time - timelps : stepsize;

        if (int_flg)
        {
            /* Do interpolation. Note that height_int always store the end time
             * height. */
#if defined(_OPENMP)
# pragma omp parallel for
#endif
            for (i = 0; i < CD->NumOsv; i++)
            {
                CD->Vcele[i].height_t =
                    CD->Vcele[i].height_o + CD->Vcele[i].height_sp * stepsize;
                CD->Vcele[i].vol = CD->Vcele[i].area * CD->Vcele[i].height_t;
            }
        }

#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (i = 0; i < nelem; i++)
        {
            int             j;

            for (j = 0; j < CD->NumSpc; j++)
            {
                if (CD->chemtype[j].mtype == 2)
                {
                    for (k = 0; k < CD->NumSsc; k++)
                    {
                        if ((CD->Totalconc[j][k + CD->NumStc] != 0) &&
                            (CD->chemtype[k + CD->NumStc].itype != AQUEOUS))
                        {
                            CD->Vcele[RT_GW(i)].t_conc[j] = CD->Vcele[RT_GW(i)].t_conc[j] -
                                CD->Totalconc[j][k + CD->NumStc] *
                                CD->Vcele[RT_GW(i)].s_conc[k] * CD->TimRiv;
                            CD->Vcele[RT_UNSAT(i)].t_conc[j] = CD->Vcele[RT_UNSAT(i)].t_conc[j] -
                                CD->Totalconc[j][k + CD->NumStc] *
                                CD->Vcele[RT_UNSAT(i)].s_conc[k] * CD->TimRiv;
                        }
                    }
                }
            }
        }

        OS3D(timelps, stepsize, CD);

        /* Total concentration except for adsorptions have been transported and
         * adjusted by the volume. For example, if no transport but volume
         * increased by rain, the concentration need be decreased. However, the
         * adsorption part has not been treated yet, so they need be adjusted by
         * the volume change.
         * The porosity is not changed during the period, so the ratio between
         * pore space before and after OS3D is the same ratio between volume of
         * porous media before and after OS3D. */
#if defined(_OPENMP)
# pragma omp parallel for
#endif
        for (i = 0; i < nelem; i++)
        {
            int             j;

            for (j = 0; j < CD->NumSpc; j++)
            {
                if (CD->chemtype[j].mtype == 2)
                {
                    for (k = 0; k < CD->NumSsc; k++)
                    {
                        if ((CD->Totalconc[j][k + CD->NumStc] != 0) &&
                            (CD->chemtype[k + CD->NumStc].itype != AQUEOUS))
                        {
                            CD->Vcele[RT_GW(i)].t_conc[j] =
                                CD->Vcele[RT_GW(i)].t_conc[j] + CD->Totalconc[j][k +
                                CD->NumStc] * CD->Vcele[RT_GW(i)].s_conc[k] *
                                CD->TimRiv;
                            CD->Vcele[RT_UNSAT(i)].t_conc[j] =
                                CD->Vcele[RT_UNSAT(i)].t_conc[j] + CD->Totalconc[j][k +
                                CD->NumStc] * CD->Vcele[RT_UNSAT(i)].s_conc[k] *
                                CD->TimRiv;
                        }
                    }
                }
            }
        }

        t_end_transp = time(NULL);
        *t_duration_transp += (t_end_transp - t_start_transp);

        t_start_react = time(NULL);

        if (int_flg)
        {
#if defined(_OPENMP)
# pragma omp parallel for
#endif
            for (i = 0; i < CD->NumVol; i++)
            {
                CD->Vcele[i].height_o = CD->Vcele[i].height_t;
                CD->Vcele[i].vol_o = CD->Vcele[i].area * CD->Vcele[i].height_o;
            }
        }

        if ((!CD->RecFlg) && ((int)(timelps + stepsize) %
            (int)(CD->React_delay * stepsize) == 0))
        {
#ifdef _OPENMP
# pragma omp parallel for
#endif
            for (i = 0; i < nelem; i++)
            {
                double          z_SOC;

                z_SOC = CD->Vcele[RT_GW(i)].maxwater -
                    (CD->Vcele[RT_GW(i)].height_t + CD->Vcele[RT_UNSAT(i)].height_t);
                z_SOC = (z_SOC > 0.0) ? z_SOC : 0.0;

                React(stepsize, CD, &CD->Vcele[RT_GW(i)], z_SOC);
                React(stepsize, CD, &CD->Vcele[RT_UNSAT(i)], z_SOC);
            }
        }

        timelps += stepsize;
        if (timelps >= end_time)
        {
            t_end_react = time(NULL);
            *t_duration_react += (t_end_react - t_start_react);
            break;
        }
    }

    if ((!CD->RecFlg) &&
        ((int)(timelps) % (int)(CD->React_delay * stepsize) == 0))
    {
        /* Do nothing. Place holder for test purposes. */
    }
}

void FreeChem(Chem_Data CD)
{
    int             i;

    free(CD->BTC_loc);
    free(CD->prepconcindex);

    for (i = 0; i < CD->NumSsc; i++)
    {
        free(CD->Dependency[i]);
    }
    free(CD->Dependency);

    for (i = 0; i < CD->NumMkr + CD->NumAkr; i++)
    {
        free(CD->Dep_kinetic[i]);
    }
    free(CD->Dep_kinetic);

    for (i = 0; i < CD->NumMin; i++)
    {
        free(CD->Dep_kinetic_all[i]);
    }
    free(CD->Dep_kinetic_all);

    for (i = 0; i < CD->NumStc; i++)
    {
        free(CD->Totalconc[i]);
#if NOT_YET_IMPLEMENTED
        free(CD->Totalconck[i]);
#endif
    }
    free(CD->Totalconc);
#if NOT_YET_IMPLEMENTED
    free(CD->Totalconck);
#endif

    free(CD->kinetics);
    free(CD->Keq);
    free(CD->KeqKinect);
    free(CD->KeqKinect_all);

    // CD->Vcele
    for (i = 0; i < CD->NumVol; i++)
    {
        free(CD->Vcele[i].t_conc);
        free(CD->Vcele[i].p_conc);
        free(CD->Vcele[i].s_conc);
        free(CD->Vcele[i].log10_pconc);
        free(CD->Vcele[i].log10_sconc);
        free(CD->Vcele[i].p_actv);
        free(CD->Vcele[i].p_para);
        free(CD->Vcele[i].p_type);
        free(CD->Vcele[i].btcv_pconc);
    }
    free(CD->Vcele);

    free(CD->Flux);

    for (i = 0; i < CD->NumStc + CD->NumSsc; i++)
    {
        free(CD->chemtype[i].ChemName);
    }
    free(CD->chemtype);

    if (CD->NumPUMP > 0)
    {
        for (i = 0; i < CD->NumPUMP; i++)
        {
            free(CD->pumps[i].Name_Species);
        }
        free(CD->pumps);
    }

    // CD->TSD_prepconc
    for (i = 0; i < CD->TSD_prepconc[0].length; i++)
    {
        free(CD->TSD_prepconc[0].data[i]);
    }
    free(CD->TSD_prepconc[0].data);
    free(CD->TSD_prepconc[0].ftime);
    free(CD->TSD_prepconc[0].value);
    free(CD->TSD_prepconc);

    free(CD->Precipitation.p_type);
    free(CD->Precipitation.t_conc);
    free(CD->Precipitation.p_conc);
    free(CD->Precipitation.p_para);

}

double Dist2Edge(const meshtbl_struct *meshtbl, const elem_struct *elem,
    int edge_j)
{
    double          para_a, para_b, para_c, x_0, x_1, y_0, y_1;
    int             index_0, index_1;

    index_0 = elem->node[(edge_j + 1) % 3] - 1;
    index_1 = elem->node[(edge_j + 2) % 3] - 1;
    x_0 = meshtbl->x[index_0];
    y_0 = meshtbl->y[index_0];
    x_1 = meshtbl->x[index_1];
    y_1 = meshtbl->y[index_1];
    para_a = y_1 - y_0;
    para_b = x_0 - x_1;
    para_c = (x_1 - x_0) * y_0 - (y_1 - y_0) * x_0;

    return fabs(para_a * elem->topo.x + para_b * elem->topo.y + para_c) /
        sqrt(para_a * para_a + para_b * para_b);
}
