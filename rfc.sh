#!/bin/sh -e
# Since we run with '#!/bin/sh -e'
#   '$? != 0' will force an exit,
# i.e. where we are interested in the result of a command,
# we have to run the command in an if-statement.

UPSTREAM=${GLUSTER_UPSTREAM}
if [ "x$UPSTREAM" = "x" ]; then
    for rmt in $(git remote); do
	rmt_repo=$(git remote show $rmt -n | grep Fetch | awk '{ print $3 }');
	if [ "${rmt_repo%.git}" = "git@github.com:gluster/glusterfs" ]; then
	    UPSTREAM=$rmt
	    echo "Picked ${UPSTREAM} as upstream remote"
	    break
	fi
    done
fi

USER_REPO=${GLUSTER_USER_REPO:-origin}
if [ "x${USER_REPO}" = "x${UPSTREAM}" ] ; then
    echo "When you submit patches, it should get submitted to your fork, not to upstream directly"
    echo "If you are not sure, check `for rmt in $(git remote); do git remote show $rmt -n; done`"
    echo "And pick the correct remote you would like to push to and do `export GLUSTER_USER_REPO=$rmt`"
    echo ""
    echo "Exiting..."
    exit 1
fi

echo "Picked ${USER_REPO} as user's remote";

FORCE="";
while getopts "vf" opt; do
    case $opt in
        v)
            # Verbose mode
            git () { >&2 echo "git $@" && `which git` $@; }
            ;;
        f)
            # Use force to git push
	    FORCE="--force";
            ;;
    esac
done
# Move the positional arguments to the beginning
shift $((OPTIND-1))


branch="devel";

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
    echo "This commit is to a non-devel branch, and hence is treated as a backport."
    echo ""
    echo "For backports we would like to retain the same gerrit Change-Id across"
    echo "branches. On auto inspection it is found that a gerrit Change-Id is"
    echo "missing, or the Change-Id is not found on your local devel branch"
    echo ""
    echo "This could mean a few things:"
    echo "    1. This is not a backport, hence choose Y on the prompt to proceed"
    echo "    2. Your $USER_REPO/devel is not up to date, hence the script is unable"
    echo "       to find the corresponding Change-Id on devel. Either choose N,"
    echo "       'git fetch', and try again, OR if you are sure you used the"
    echo "       same Change-Id, choose Y at the prompt to proceed"
    echo "    3. You commented or removed the Change-Id in your commit message after"
    echo "       cherry picking the commit. Choose N, fix the commit message to"
    echo "       use the same Change-Id as 'devel' (git commit --amend), resubmit"
    echo ""
}

check_backport()
{
    moveon='N'

    # Backports are never made to 'devel'
    if [ $branch = "devel" ]; then
        return;
    fi

    # Extract the change ID from the commit message
    changeid=$(git log -n1 --format='%b' | grep -i '^Change-Id: ' | awk '{print $2}')

    # If there is no change ID ask if we should continue
    if [ -z "$changeid" ]; then
        backport_id_message;
        echo -n "Did not find a Change-Id for a possible backport. Continue (y/N): "
        read moveon
    else
        # Search 'devel' for the same change ID (rebase_changes has run, so we
        # should never not find a Change-Id)
        mchangeid=$(git log ${UPSTREAM}/devel --format='%b' --grep="^Change-Id: ${changeid}" | grep ${changeid} | awk '{print $2}')

        # Check if we found the change ID on 'devel', else throw a message to
        # decide if we should continue.
        # NOTE: If 'devel' was not rebased, we will not find the Change-ID and
        # could hit a false positive case here (or if someone checks out some
        # other branch as 'devel').
        if [ "${mchangeid}" = "${changeid}" ]; then
            moveon="Y"
        else
            backport_id_message;
            echo "Change-Id of commit: $changeid"
            echo "Change-Id on devel: $mchangeid"
            echo -n "Did not find mentioned Change-Id on 'devel' for a possible backport. Continue (y/N): "
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
    GIT_EDITOR=$0 git rebase -i $UPSTREAM/$branch;
}


# Regex elaborated:
#   grep options:
#     -w -> --word-regexp (from the man page)
#        Select only those lines containing matches that form whole words.
#        The test is that the matching substring must either be at the
#        beginning of the line, or preceded by a  non-word  constituent
#        character.  Similarly, it must be either at the end of the line or
#        followed by a non-word constituent character.  Word-constituent
#        characters are letters, digits, and the underscore.
#
#        IOW, the above helps us find the pattern with leading or training
#        spaces or non word consituents like , or ;
#
#     -i -> --ignore-case (case insensitive search)
#
#     -o -> --only-matching (only print matching portion of the line)
#
#     -E -> --extended-regexp (use extended regular expression)
#
#   ^
#      The search begins at the start of each line
#
#   [[:space:]]*
#      Any number of spaces is accepted
#
#   (Fixes|Updates)
#      Finds 'Fixes' OR 'Updates' in any case combination
#
#   (:)?
#      Followed by an optional : (colon)
#
#   [[:space:]]+
#      Followed by 1 or more spaces
#
#   #
#      Followed by #
#
#   [[:digit:]]+
#      Followed by 1 or more digits
REFRE="^[[:space:]]*(Fixes|Updates)(:)?[[:space:]]+#[[:digit:]]+"

editor_mode()
{
    if [ $(basename "$1") = "git-rebase-todo" ]; then
        sed 's/^pick /reword /g' "$1" > $1.new && mv $1.new $1;
        return;
    fi

    if [ $(basename "$1") = "COMMIT_EDITMSG" ]; then
        # see note above function warn_reference_missing for regex elaboration
        # Lets first check for github issues
        ref=$(git log -n1 --format='%b' | grep -iow -E "${REFRE}" | awk -F '#' '{print $2}');
        if [ "x${ref}" != "x" ]; then
            return;
        fi

        while true; do
            echo Commit: "\"$(head -n 1 $1)\""
            echo -n "Github Issue ID: "
            read issue
            if [ -z "$issue" ]; then
                return;
            fi
            if ! is_num "$issue"; then
                echo "Invalid Github Issue ID!!!";
                continue;
            fi

            echo "Select yes '(y)' if this patch fixes the issue/feature completely,"
            echo -n "or is the last of the patchset which brings feature (Y/n): "
            read fixes
            fixes_string="Fixes"
            if [ "${fixes}" = 'N' ] || [ "${fixes}" = 'n' ]; then
                fixes_string="Updates"
            fi

            sed "/^Change-Id:/{p; s/^.*$/${fixes_string}: #${issue}/;}" $1 > $1.new && \
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
    git diff $UPSTREAM/$branch..HEAD | grep -q .;
}


warn_reference_missing()
{
    echo ""
    echo "=== Missing a reference in commit! ==="
    echo ""
    echo "Gluster commits are made with a reference to a github issue"
    echo ""
    echo "A check on the commit message, reveals that there is no "
    echo "github issue referenced in the commit message."
    echo ""
    echo "https://github.com/gluster/glusterfs/issues/new"
    echo ""
    echo "Please open an issue and reference the same in the commit message "
    echo "using the following tags:"
    echo ""
    echo "\"Fixes: #NNNN\" OR \"Updates: #NNNN\","
    echo "where NNNN is the issue id"
    echo ""
    echo "You may abort the submission choosing 'N' below and use"
    echo "'git commit --amend' to add the issue reference before posting"
    echo "to gerrit."
    echo ""
    echo -n "Missing reference to a github issue. Continue (y/N): "
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

    git fetch $UPSTREAM;

    rebase_changes;

    check_backport;

    assert_diverge;

    # see note above variable REFRE for regex elaboration
    reference=$(git log -n1 --format='%b' | grep -iow -E "${REFRE}" | awk -F '#' '{print $2}');

    # If this is a commit against 'devel' and does not have a github
    # issue reference. Warn the contributor that one of the 2 is required
    if [ -z "${reference}" ] && [ $branch = "devel" ]; then
        warn_reference_missing;
    fi

    # TODO: add clang-format command here. It will after the changes are done everywhere else
    clang_format=$(clang-format --version)
    if [ ! -z "${clang_format}" ]; then
        # Considering git show may not give any files as output matching the
        # criteria, good to tell script not to fail on error
        set +e
        list_of_files=$(git show --pretty="format:" --name-only |
                            grep -v "contrib/" | egrep --color=never "*\.[ch]$");
        if [ ! -z "${list_of_files}" ]; then
            echo "${list_of_files}" | xargs clang-format -i
        fi
        set -e
    else
        echo "High probability of your patch not passing smoke due to coding standard check"
        echo "Please install 'clang-format' to format the patch before submitting"
    fi

    if [ "$DRY_RUN" = 1 ]; then
        drier='echo -e Please use the following command to send your commits to review:\n\n'
    else
        drier=
    fi

    if [ -z "${reference}" ]; then
        $drier git push $USER_REPO HEAD:temp_${branch}/$(date +%Y-%m-%d_%s) $FORCE;
    else
        $drier git push $USER_REPO HEAD:issue${reference}_${branch} $FORCE;
    fi
}

main "$@"
