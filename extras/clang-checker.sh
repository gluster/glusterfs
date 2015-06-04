#!/usr/bin/env bash
#*******************************************************************************
#                                                                              *
#  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>                    *
#  This file is part of GlusterFS.                                             *
#                                                                              *
#  This file is licensed to you under your choice of the GNU Lesser            *
#  General Public License, version 3 or any later version (LGPLv3 or           *
#  later), or the GNU General Public License, version 2 (GPLv2), in all        *
#  cases as published by the Free Software Foundation.                         *
#------------------------------------------------------------------------------*
#                                                                              *
# clang-checker.sh:  This script runs clang static analyzer using 'scan-build' *
#                    a perl wrapper. After you commit your patch i.e. right    *
#                    before executing rfc.sh in order to push the patch to     *
#                    repository, it is recommended that you execute            *
#                    clang-checker.sh to perform static analysis inorder to    *
#                    check if there are any possible bugs in the code.         *
#                                                                              *
#                    This script performs the static analysis with and      *
#                    without HEAD commit, it runs the analyzer only in the     *
#                    directory where changes have been made and finally diff's *
#                    the number of bugs using both cases (i.e. with and        *
#                    without your commit) and gives a summary, which explain's *
#                    about the eligibility of your patch.                      *
#                                                                              *
# Usage:             $ cd $PATH_TO_GLUSTERFS                                   *
#                    $ extras/clang-checker.sh (or) $ make clang-check         *
#                                                                              *
# Author:            Prasanna Kumar Kalever <prasanna.kalever@redhat.com>      *
#                                                                              *
#*******************************************************************************

REPORTS_DIR=$(pwd)
BASELINE_DIR=${REPORTS_DIR}/baseline
BRESULTS_DIR=${BASELINE_DIR}/results
BBACKUP_DIR=${BASELINE_DIR}/backup
TARGET_DIR=${REPORTS_DIR}/target
TRESULTS_DIR=${TARGET_DIR}/results
TBACKUP_DIR=${TARGET_DIR}/backup

declare -A DICT_B
declare -A DICT_T
declare -A ARR
declare -A FILES

function identify_changes () {
    MODIFIED_DATA=$(git show --name-status --oneline | tail -n +2)
    FLAG=0
    for i in ${MODIFIED_DATA}; do
        if [ $FLAG -eq 1 ]; then
            ARR+="$(dirname $i) ";
            FLAG=0;
        fi
        if [ $i = 'M' ] || [ $i = 'A' ]; then
            FLAG=1;
        fi
    done

    MODIFIED_DIR=$(echo "${ARR[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' ')
    for i in $MODIFIED_DIR; do
        # run only in directories which has Makefile
        if [ $(find ./$i -iname "makefile*" | wc -c) -gt 0 ]; then
            # skip 'doc' and '.'(top) directory
            if [ "xx$i" != "xxdoc" ] && [ "xx$i" != "xx." ]; then
                FILES+="$i "
            fi
        fi
    done
    if [ -z $FILES ]; then
        echo "Probably no changes made to 'c' files"
        exit;
    fi
}

function check_prerequisites () {
    if ! type "clang" 2> /dev/null; then
        echo -e "\ntry after installing clang and scan-build..."
        echo    "useful info at http://clang-analyzer.llvm.org/installation.html\n"
        echo -e "hint: 'dnf -y install clang-analyzer.noarch'\n"
        exit 1;
    elif ! type "scan-build" 2> /dev/null; then
        echo -e "\ntry after installing scan-build..."
        echo    "useful info at http://clang-analyzer.llvm.org/installation.html"
        echo -e "hint: 'dnf -y install clang-analyzer.noarch'\n"
        exit 1;
    fi
}

function force_terminate () {
    echo -e "\nreceived a signal to force terminate ..\n"
    git am --abort 2> /dev/null
    git am ${PATCH_NAME}
    rm -f ${REPORTS_DIR}/${PATCH_NAME}
    exit 1;
}

function run_scanbuild () {
    local CLANG=$(which clang)
    local SCAN_BUILD=$(which scan-build)
    local ORIG_COMMIT=$(git rev-parse --verify HEAD^)
    PATCH_NAME=$(git format-patch $ORIG_COMMIT)

    echo -e "\n| Performing clang analysis on:" \
            "$(git log --pretty=format:"%h - '%s' by %an" -1) ... |\n"
    echo -e "Changes are identified in '${FILES[@]}' directorie[s]\n"

    if [ -d "${BRESULTS_DIR}" ]; then
        mkdir -p ${BBACKUP_DIR} ${TBACKUP_DIR}
        mv ${BRESULTS_DIR} \
           ${BBACKUP_DIR}/results_$(ls -l ${BBACKUP_DIR} | wc -l)
        mv ${TRESULTS_DIR} \
           ${TBACKUP_DIR}/results_$(ls -l ${TBACKUP_DIR} | wc -l)
    fi
    for DIR in ${FILES[@]}; do
        mkdir -p ${BRESULTS_DIR}/$(echo ${DIR} | sed 's/\//_/g')
        mkdir -p ${TRESULTS_DIR}/$(echo ${DIR} | sed 's/\//_/g')
    done
    # get nproc info
    case $(uname -s) in
    'Linux')
        local NPROC=$(getconf _NPROCESSORS_ONLN)
        ;;
    'NetBSD')
        local NPROC=$(getconf NPROCESSORS_ONLN)
        ;;
    esac

    trap force_terminate INT TERM QUIT EXIT

    git reset --hard HEAD^

    # build complete source code for sake of dependencies
    echo -e "\n# make -j${NPROC} ..."
    make -j${NPROC} 1>/dev/null

    for DIR in ${FILES[@]}; do
        if [ $(find ./$i -iname "makefile*" | wc -c) -gt 0 ]; then
            make clean -C ${DIR} 1>/dev/null
            echo -e "\n| Analyzing ${DIR} without commit ... |\n"
            # run only in directory where changes are made
            ${SCAN_BUILD} -o ${BRESULTS_DIR}/$(echo ${DIR} | sed 's/\//_/g') \
                          --use-analyzer=${CLANG} make -j${NPROC} -C ${DIR}
        fi
    done

    echo -e "\n| Analyzing without commit complete ... |\n"

    git am ${PATCH_NAME}
    trap - INT TERM QUIT EXIT

    # In case commit has changes to configure stuff ?
    echo -e "\n# make clean ..."
    make clean 1>/dev/null
    echo -e "\n# ./autogen.sh && ./configure --with-previous-options ..."
    ${REPORTS_DIR}/autogen.sh 2>/dev/null
    ${REPORTS_DIR}/configure  --with-previous-options 1>/dev/null
    echo -e "\n# make -j${NPROC} ..."
    make -j${NPROC} 1>/dev/null

    for DIR in ${FILES[@]}; do
        if [ $(find ./$i -iname "makefile*" | wc -c) -gt 0 ]; then
            make clean -C ${DIR} 1>/dev/null
            echo -e "\n| Analyzing ${DIR} with commit ... |\n"
            # run only in directory where changes are made
            ${SCAN_BUILD} -o ${TRESULTS_DIR}/$(echo ${DIR} | sed 's/\//_/g') \
                          --use-analyzer=${CLANG} make -j${NPROC} -C ${DIR}
        fi
    done

    echo -e "\n| Analyzing with commit complete ... |\n"

    rm -f ${REPORTS_DIR}/${PATCH_NAME}
}

function count_for_baseline () {
    for DIR in ${FILES[@]}; do
        HTMLS_DIR=${BRESULTS_DIR}/$(echo ${DIR} |
                    sed 's/\//_/g')/$(ls ${BRESULTS_DIR}/$(echo ${DIR} |
                    sed 's/\//_/g')/);

        local NAMES_OF_BUGS_B=$(grep -n "SUMM_DESC" ${HTMLS_DIR}/index.html |
                                cut -d"<" -f3 | cut -d">" -f2 |
                                sed 's/[^a-zA-Z0]/_/g' | tr '\n' ' ')
        local NO_OF_BUGS_B=$(grep -n "SUMM_DESC" ${HTMLS_DIR}/index.html |
                             cut -d"<" -f5 | cut -d">" -f2 | tr '\n' ' ')
        local count_B=0;

        read -a BUG_NAME_B <<<$NAMES_OF_BUGS_B
        read -a BUG_COUNT_B <<<$NO_OF_BUGS_B
        for i in ${BUG_NAME_B[@]};
        do
            if [ ! -z ${DICT_B[$i]} ]; then
                DICT_B[$i]=$(expr ${BUG_COUNT_B[count_B]} + ${DICT_B[$i]});
            else
                DICT_B+=([$i]=${BUG_COUNT_B[count_B]});
            fi
            count_B=$(expr $count_B + 1)
        done
    done

    echo -e "\nBASELINE BUGS LIST (before applying patch):"
    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
    for key_B in ${!DICT_B[@]}; do
        echo "${key_B} --> ${DICT_B[${key_B}]}" | sed 's/_/ /g' | tr -s ' '
    done
}

function count_for_target () {
    for DIR in ${FILES[@]}; do
        HTMLS_DIR=${TRESULTS_DIR}/$(echo ${DIR} |
                    sed 's/\//_/g')/$(ls ${TRESULTS_DIR}/$(echo ${DIR} |
                    sed 's/\//_/g')/);

        local NAME_OF_BUGS_T=$(grep -n "SUMM_DESC" ${HTMLS_DIR}/index.html |
                              cut -d"<" -f3 | cut -d">" -f2 |
                              sed 's/[^a-zA-Z0]/_/g'| tr '\n' ' ')
        local NO_OF_BUGS_T=$(grep -n "SUMM_DESC" ${HTMLS_DIR}/index.html |
                             cut -d"<" -f5 | cut -d">" -f2 | tr '\n' ' ')
        local count_T=0;

        read -a BUG_NAME_T <<<$NAME_OF_BUGS_T
        read -a BUG_COUNT_T <<<$NO_OF_BUGS_T

        for i in ${BUG_NAME_T[@]};
        do
            if [ ! -z ${DICT_T[$i]} ]; then
                DICT_T[$i]=$(expr ${BUG_COUNT_T[count_T]} + ${DICT_T[$i]});
            else
                DICT_T+=([$i]=${BUG_COUNT_T[count_T]});
            fi
            count_T=$(expr $count_T + 1)
        done
    done

    echo -e "\nTARGET BUGS LIST (after applying patch):"
    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
    for key_T in ${!DICT_T[@]}; do
        echo "${key_T} --> ${DICT_T[${key_T}]}" | sed 's/_/ /g' | tr -s ' '
    done
}

function array_contains () {
    local SEEKING=$1; shift
    local IN=1
    for ELEMENT; do
        if [[ $ELEMENT == $SEEKING ]]; then
            IN=0
            break
        fi
    done
    return $IN
}

function main () {
    echo -e "\n================ Clang analyzer in progress ================\n"
    check_prerequisites
    identify_changes
    run_scanbuild
    clear
    count_for_baseline
    count_for_target
    echo -e "\nSUMMARY OF CLANG-ANALYZER:"
    echo "~~~~~~~~~~~~~~~~~~~~~~~~~~"

    FLAG=0
    for BUG in ${!DICT_T[@]}; do
        array_contains $BUG "${!DICT_B[@]}"
        if [ $? -eq 1 ]; then
            echo "New ${DICT_T[${BUG}]} Bug[s] introduced: $(echo $BUG |
                                                            sed 's/_/ /g' |
                                                            tr -s ' ')"
            FLAG=1
        else
            if [ ${BUG} != "All_Bugs" ]; then
                if [ ${DICT_B[${BUG}]} -lt            \
                    ${DICT_T[${BUG}]} ]; then
                    echo "Extra $(expr ${DICT_T[${BUG}]} - \
                          ${DICT_B[${BUG}]}) Bug[s] Introduced in: $(echo $BUG |
                          sed 's/_/ /g' | tr -s ' ')"
                    FLAG=1
                fi
            fi
        fi
    done

    echo
    if [ $FLAG -eq 0 ]; then
        echo -e "Patch Value given by Clang analyzer '+1'\n"
    else
        echo -e "Patch Value given by Clang analyzer '-1'\n"
    fi
    echo -e "\nExplore complete results at:"
    find ${BRESULTS_DIR}/ -iname "index.html"
    find ${TRESULTS_DIR}/ -iname "index.html"
    echo -e "\n================= Done with Clang Analysis =================\n"

    exit ${FLAG}
}

main
