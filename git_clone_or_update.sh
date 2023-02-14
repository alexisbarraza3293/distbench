################################################################################
# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
#
# git_clone_or_update.sh:
# Functions for efficiently mirroring external git repos without resorting to
# submodules. Note: this uses auxilary worktrees, which may not be what you
# are used to.

function git_clone_or_update() {
  if [[ $# != "4" ]]
  then
    echo_magenta "git_clone_or_update needs exactly 4 arguments to proceed"
   return 1
  fi
  REMOTE_URL="${1}"
  TAGBRANCH="${2}"
  GITDIR="$(readlink -smn ${3})"
  WORKTREE="$(readlink -smn ${4})"
  REALPWD="$(readlink -smn ${PWD})"
  echo_magenta "\\nUpdating to branch ${TAGBRANCH} of ${REMOTE_URL} ..."
  case "${GITDIR}" in
    ${REALPWD}/*)
      # echo_magenta "git_clone_or_update output dir is within current dir :-)"
      ;;
    *)
      echo_red "\\nError: git_clone_or_update output dir must be within current"
      return 2
      ;;
  esac
  case "${WORKTREE}" in
    ${GITDIR}/*)
      # echo_magenta "git_clone_or_update worktree dir is within GITDIR :-)"
      ;;
    *)
      echo_red "\\nError: git_clone_or_update worktree must be within GITDIR"
      return 3
      ;;
  esac
  TOP_LEVEL_URL="$(git remote get-url origin 2> /dev/null || true)"
  if [[ "${TOP_LEVEL_URL}" == "${REMOTE_URL}" ]]
  then
    echo_red "\\nError: looks like we are trying to nest a repo inside itself."
    return 4
  fi

  OLD_URL="$(git -C "${GITDIR}" remote get-url origin 2> /dev/null || true)"
  if [[ -n "${OLD_URL}" && -n $(git -C "${GITDIR}" rev-parse --show-cdup) ]]
  then
    echo_red "\\nLocal git repo may be corrupted!!!"
    echo_red "  Refusing to overwrite anything..."
    echo_red "  You might try running"
    echo_blue "  rm -rf $GITDIR"
    echo_red "  and trying again if you are sure that this is safe."
    return 5
  fi

  # Check if we need to switch to a new upstream repo:
  if [[ "${OLD_URL}" != "$REMOTE_URL" ]]
  then
    if [[ -e "${GITDIR}" ]]
    then
      if [[ ! -d "${GITDIR}" ]]
      then
        echo_red "\\nError: ${GITDIR} exists, but is not a directory"
        return 6
      fi
      echo_magenta "\\nLooks like we are trying to switch to a new remote..."
      ! git -C "${GITDIR}" remote remove old_origin
      git -C "${GITDIR}" remote rename origin old_origin
    fi
    mkdir -p "${GITDIR}"
    git -C "${GITDIR}" init
    git -C "${GITDIR}" remote add origin "${REMOTE_URL}"
  fi
  if [[ -d "${WORKTREE}" ]]
  then
    pushd "${WORKTREE}"
    # Make sure the worktree is a functional worktree of the right git repo!!!

    if [[ -n "$(git rev-parse --show-prefix)" || \
          "$(git rev-parse --git-dir)" != \
              "${GITDIR}/.git/worktrees/${TAGBRANCH}" ]]
    then
      echo_red "\\nLocal git worktree may be corrupted!!!"
      echo_red "  Refusing to overwrite anything..."
      echo_red "  You might try running"
      echo_blue "  rm -rf $GITDIR"
      echo_red "  and trying again if you are sure that this is safe."
      return 7
    fi

    # Make sure worktree does not contain uncommited modifications.
    if ! git diff --exit-code HEAD
    then
      echo_red "\\nThere may be modifications to your worktree files."
      echo_red "Refusing to overwrite anything..."
      return 8
    fi
    # Make sure working tree is a commit that existed in a remote branch
    # at the time of the last update. If a branch was force pushed we
    # won't see the new value yet, but this is on purpose. If people are
    # force pushing they must want to delete history here too.
    if [[ -z "$(git branch -r --contains HEAD ; git tag --contains HEAD)" ]]
    then
      echo_red "\\nThe local git worktree no longer matches anything upstream."
      echo_red "This probably means you made local changes and commited them."
      echo_red "Refusing to overwrite anything..."
      return 9
    fi
    popd
  fi
  git -C "${GITDIR}" fetch origin --tags --force --prune
  if [[ ! -d "${WORKTREE}" ]]
  then
    git -C "${GITDIR}" worktree add "${WORKTREE}" "${TAGBRANCH}"
  fi

  # Finally: determine if we were fed a tag or a branch, and actually update
  # the local working tree.
  if git -C "${WORKTREE}" rev-parse --verify origin/${TAGBRANCH}^{commit} &> /dev/null
  then
    git -C "${WORKTREE}" reset --hard origin/${TAGBRANCH} --
  else
    git -C "${WORKTREE}" reset --hard ${TAGBRANCH} --
  fi
}
