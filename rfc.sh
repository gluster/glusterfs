#!/bin/sh -e
# Since we run with '#!/bin/sh -e'
#   '$? != 0' will force an exit,
# i.e. where we are interested in the result of a command,
# we have to run the command in an if-statement.

ORIGIN=${GLUSTER_ORIGIN:-origin}

while getopts "v" opt; do
    case $opt in
        v)
            # Verbose mode
            git () { >&2 echo "git $@" && `which git` $@; }
            ;;
    esac
done
# Move the positional arguments to the beginning
shift $((OPTIND-1))


branch="release-4.1";

set_hooks_commit_msg()
{
    f=".git/hooks/commit-msg";
    u="http://review.gluster.org/tools/hooks/commit-msg";

    if [ -x "$f" ]; then
        return;
    fi

    curl -L -o $f $u || wget -O $f $u;

    chmod +x $f

    # Let the 'Change-Id: ' header get assigned on first run of rfc.sh
    GIT_EDITOR=true git commit --amend;
}


is_num()
{
    local num;

    num="$1";

    [ -z "$(echo $num | sed -e 's/[0-9]//g')" ]
}

backport_id_message()
{
    echo ""
    echo "This commit is to a non-master branch, and hence is treated as a backport."
    echo ""
    echo "For backports we would like to retain the same gerrit Change-Id across"
    echo "branches. On auto inspection it is found that a gerrit Change-Id is"
    echo "missing, or the Change-Id is not found on your local master"
    echo ""
    echo "This could mean a few things:"
    echo "    1. This is not a backport, hence choose Y on the prompt to proceed"
    echo "    2. Your $ORIGIN master is not up to date, hence the script is unable"
    echo "       to find the corresponding Change-Id on master. Either choose N,"
    echo "       'git fetch', and try again, OR if you are sure you used the"
    echo "       same Change-Id, choose Y at the prompt to proceed"
    echo "    3. You commented or removed the Change-Id in your commit message after"
    echo "       cherry picking the commit. Choose N, fix the commit message to"
    echo "       use the same Change-Id as master (git commit --amend), resubmit"
    echo ""
}

check_backport()
{
    moveon='N'

    # Backports are never made to master
    if [ $branch = "master" ]; then
        return;
    fi

    # Extract the change ID from the commit message
    changeid=$(git show --format='%b' | grep -i '^Change-Id: ' | awk '{print $2}')

    # If there is no change ID ask if we should continue
    if [ -z "$changeid" ]; then
        backport_id_message;
        echo -n "Did not find a Change-Id for a possible backport. Continue (y/N): "
        read moveon
    else
        # Search master for the same change ID (rebase_changes has run, so we
        # should never not find a Change-Id)
        mchangeid=$(git log $ORIGIN/master --format='%b' --grep="^Change-Id: ${changeid}" | grep ${changeid} | awk '{print $2}')

        # Check if we found the change ID on master, else throw a message to
        # decide if we should continue.
        # NOTE: If master was not rebased, we will not find the Change-ID and
        # could hit a false positive case here (or if someone checks out some
        # other branch as master).
        if [ "${mchangeid}" = "${changeid}" ]; then
            moveon="Y"
        else
            backport_id_message;
            echo "Change-Id of commit: $changeid"
            echo "Change-Id on master: $mchangeid"
            echo -n "Did not find mentioned Change-Id on master for a possible backport. Continue (y/N): "
            read moveon
        fi
    fi

    if [ "${moveon}" = 'Y' ] || [ "${moveon}" = 'y' ]; then
        return;
    else
        exit 1
    fi
}


rebase_changes()
{
    GIT_EDITOR=$0 git rebase -i $ORIGIN/$branch;
}


editor_mode()
{
    if [ $(basename "$1") = "git-rebase-todo" ]; then
        sed 's/^pick /reword /g' "$1" > $1.new && mv $1.new $1;
        return;
    fi

    if [ $(basename "$1") = "COMMIT_EDITMSG" ]; then
        # see note above function warn_reference_missing for regex elaboration
        # Lets first check for github issues
        ref=$(git show --format='%b' | grep -ow -E "([fF][iI][xX][eE][sS]|[uU][pP][dD][aA][tT][eE][sS])(:)?[[:space:]]+(gluster\/glusterfs)?#[[:digit:]]+" | awk -F '#' '{print $2}');
        if [ "x${ref}" = "x" ]; then
            # if not found, check for bugs
            ref=$(git show --format='%b' | grep -ow -E "([fF][iI][xX][eE][sS]|[uU][pP][dD][aA][tT][eE][sS])(:)?[[:space:]]+bz#[[:digit:]]+" | awk -F '#' '{print $2}');
        fi

        if [ "x${ref}" != "x" ]; then
            return;
        fi

        while true; do
            echo Commit: "\"$(head -n 1 $1)\""
            echo -n "Reference (Bugzilla ID or Github Issue ID): "
            read bug
            if [ -z "$bug" ]; then
                return;
            fi
            if ! is_num "$bug"; then
                echo "Invalid reference ID ($bug)!!!";
                continue;
            fi

            bz_string="bz"
            if [ $bug -lt 742000 ]; then
                bz_string=""
            fi

            echo "Select yes '(y)' if this patch fixes the bug/feature completely,"
            echo -n "or is the last of the patchset which brings feature (Y/n): "
            read fixes
            fixes_string="fixes"
            if [ "${fixes}" = 'N' ] || [ "${fixes}" = 'n' ]; then
                fixes_string="updates"
            fi

            sed "/^Change-Id:/{p; s/^.*$/${fixes_string}: ${bz_string}#${bug}/;}" $1 > $1.new && \
                mv $1.new $1;
            return;
        done
    fi

    cat <<EOF
$0 - editor_mode called on unrecognized file $1 with content:
$(cat $1)
EOF
    return 1;
}


assert_diverge()
{
    git diff $ORIGIN/$branch..HEAD | grep -q .;
}


check_patches_for_coding_style()
{
    git fetch $ORIGIN;

    check_patch_script=./build-aux/checkpatch.pl
    if [ ! -e ${check_patch_script} ] ; then
        echo "${check_patch_script} is not executable .. abort"
        exit 1
    fi

    # The URL of our Gerrit server
    export GERRIT_URL="review.gluster.org"

    echo "Running coding guidelines check ..."
    head=$(git rev-parse --abbrev-ref HEAD)
    # Kludge: "1>&2 && echo $? || echo $?" is to get around
    #         "-e" from script invocation
    RES=$(git format-patch --stdout $ORIGIN/${branch}..${head} \
          | ${check_patch_script} --strict --terse - 1>&2 && echo $? || echo $?)
    if [ "$RES" -eq 1 ] ; then
        echo "Errors caught, get details by:"
        echo "  git format-patch --stdout  $ORIGIN/${branch}..${head} \\"
        echo "  | ${check_patch_script} --strict --gerrit-url ${GERRIT_URL} -"
        echo "and correct errors"
        exit 1
    elif [ "$RES" -eq 2 ] ; then
        echo "Warnings or checks caught, get details by:"
        echo "  git format-patch --stdout  $ORIGIN/${branch}..${head} \\"
        echo "  | ${check_patch_script} --strict --gerrit-url ${GERRIT_URL} -"
        echo -n "Do you want to continue anyway [no/yes]: "
        read yesno
        if [ "${yesno}" != "yes" ] ; then
            echo "Aborting..."
            exit 1
        fi
    fi
}

# Regex elaborated:
#   grep -w -> --word-regexp (from the man page)
#      Select only those lines containing matches that form whole words.
#      The test is that the matching substring must either be at the
#      beginning of the line, or preceded by a  non-word  constituent
#      character.  Similarly, it must be either at the end of the line or
#      followed by a non-word constituent character.  Word-constituent
#      characters are letters, digits, and the underscore.
#   IOW, the above helps us find the pattern with leading or training spaces
#   or non word consituents like , or ;
#
#   [fF][iI][xX][eE][sS]|[uU][pP][dD][aA][tT][eE][sS])
#      Finds 'fixes' OR 'updates' in any case combination
#
#   (:)?
#      Followed by an optional : (colon)
#
#   [[:space:]]+
#      followed by 1 or more spaces
#
#   (gluster\/glusterfs)?
#      Followed by 0 or more gluster/glusterfs
#
#   #
#      Followed by #
#
#   [[:digit:]]+
#      Followed by 1 or more digits
warn_reference_missing()
{
    echo ""
    echo "=== Missing a reference in commit! ==="
    echo ""
    echo "Gluster commits are made with a reference to a bug or a github issue"
    echo ""
    echo "Submissions that are enhancements (IOW, not functional"
    echo "bug fixes, but improvements of any nature to the code) are tracked"
    echo "using github issues [1]."
    echo ""
    echo "Submissions that are bug fixes are tracked using Bugzilla [2]."
    echo ""
    echo "A check on the commit message, reveals that there is no bug or"
    echo "github issue referenced in the commit message"
    echo ""
    echo "[1] https://github.com/gluster/glusterfs/issues/new"
    echo "[2] https://bugzilla.redhat.com/enter_bug.cgi?product=GlusterFS"
    echo ""
    echo "Please file an issue or a bug report and reference the same in the"
    echo "commit message using the following tags:"
    echo "GitHub Issues:"
    echo "\"Fixes: gluster/glusterfs#n\" OR \"Updates: gluster/glusterfs#n\","
    echo "\"Fixes: #n\" OR \"Updates: #n\","
    echo "Bugzilla ID:"
    echo "\"Fixes: bz#n\" OR \"Updates: bz#n\","
    echo "where n is the issue or bug number"
    echo ""
    echo "You may abort the submission choosing 'N' below and use"
    echo "'git commit --amend' to add the issue reference before posting"
    echo "to gerrit."
    echo ""
    echo -n "Missing reference to a bug or a github issue. Continue (y/N): "
    read moveon
    if [ "${moveon}" = 'Y' ] || [ "${moveon}" = 'y' ]; then
        return;
    else
        exit 1
    fi
}

main()
{
    set_hooks_commit_msg;

    # rfc.sh calls itself from rebase_changes, which uses rfc.sh as the EDITOR
    # thus, getting the commit message to work with in the editor_mode.
    if [ -e "$1" ]; then
        editor_mode "$@";
        return;
    fi

    check_patches_for_coding_style;

    rebase_changes;

    check_backport;

    assert_diverge;

    # see note above function warn_reference_missing for regex elaboration
    reference=$(git show --format='%b' | grep -ow -E "([fF][iI][xX][eE][sS]|[uU][pP][dD][aA][tT][eE][sS])(:)?[[:space:]]+(gluster\/glusterfs)?(bz)?#[[:digit:]]+" | awk -F '#' '{print $2}');

    # If this is a commit against master and does not have a bug ID or a github
    # issue reference. Warn the contributor that one of the 2 is required
    if [ -z "${reference}" ] && [ $branch = "master" ]; then
        warn_reference_missing;
    fi

    if [ "$DRY_RUN" = 1 ]; then
        drier='echo -e Please use the following command to send your commits to review:\n\n'
    else
        drier=
    fi

    if [ -z "${reference}" ]; then
        $drier git push $ORIGIN HEAD:refs/for/$branch/rfc;
    else
        $drier git push $ORIGIN HEAD:refs/for/$branch/ref-${reference};
    fi
}

main "$@"
