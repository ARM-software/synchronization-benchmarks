Contributing to Synchronization-Benchmarks
==========================================

Getting Started
---------------

-  Make sure you have a `GitHub account`_.
-  Create an `issue`_ for your work if one does not already exist. This gives
   everyone visibility of whether others are working on something similar.

   -  If you intend to include Third Party IP in your contribution, please
      raise a separate `issue`_ for this and ensure that the changes that
      include Third Party IP are made on a separate topic branch.

-  `Fork`_ `synchronization-benchmarks`_ on GitHub.
-  Clone the fork to your own machine.
-  Create a local topic branch based on the `synchronization-benchmarks`_ ``master``
   branch.
-  Make sure you have placed the hooks/commit-msg hook into your .git/hooks directory
   to append change IDs to your commits.

Making Changes
--------------

-  Make commits of logical units. See these general `Git guidelines`_ for
   contributing to a project.
-  Keep the commits on topic. If you need to fix another bug or make another
   enhancement, please create a separate `issue`_ and address it on a separate
   topic branch.
-  Avoid long commit series. If you do have a long series, consider whether
   some commits should be squashed together or addressed in a separate topic.
-  Make sure your commit messages are in the proper format. If a commit fixes
   a GitHub `issue`_, include a reference; this ensures the `issue`_ is
   `automatically closed`_ when merged into the `synchronization-benchmarks`_ ``master``
   branch.
-  Where appropriate, please update the documentation and license of files.

   -  Ensure that each changed file has the correct copyright and license
      information. Files that entirely consist of contributions to this
      project should have the copyright notice and BSD-3-Clause SPDX license
      identifier as shown in `license.rst`_. Files that contain
      changes to imported Third Party IP should contain a notice as follows,
      with the original copyright and license text retained:

      ::

        Portions copyright (c) [XXXX-]YYYY, ARM Limited and Contributors. All rights reserved.

      where XXXX is the year of first contribution (if different to YYYY) and
      YYYY is the year of most recent contribution.
   -  For topics with multiple commits, you should make all documentation
      changes (and nothing else) in the last commit of the series. Otherwise,
      include the documentation changes within the single commit.

Submitting Changes
------------------

-  We prefer that each commit in the series has at least one ``Signed-off-by:``
   line, using your real name and email address, but it is not required.
-  Push your local changes to your fork of the repository.
-  Submit a `pull request`_ to the `synchronization-benchmarks`_ ``integration`` branch.

   -  The changes in the `pull request`_ will then undergo further review.
      Any review comments will be made as comments on the `pull request`_.
      This may require you to do some rework.

-  When the changes are accepted, the maintainer of the repository will integrate them.

   -  Typically, the Maintainers will merge the `pull request`_ into the
      ``integration`` branch within the GitHub UI, creating a merge commit.
   -  Please avoid creating merge commits in the `pull request`_ itself.
   -  If the `pull request`_ is not based on a recent commit, the Maintainers
      may rebase it onto the ``master`` branch first, or ask you to do this.
   -  If the `pull request`_ cannot be automatically merged, the Maintainers
      will ask you to rebase it onto the ``master`` branch.
   -  After final integration testing, the Maintainers will push your merge
      commit to the ``master`` branch. If a problem is found during integration,
      the merge commit will be removed from the ``integration`` branch and the
      Maintainers will ask you to create a new pull request to resolve the
      problem.
   -  Please do not delete your topic branch until it is safely merged into
      the ``master`` branch.

--------------

*Copyright (c) 2018, ARM Limited and Contributors. All rights reserved.*

.. _GitHub account: https://github.com/signup/free
.. _issue: https://github.com/ARM-software/synchronization-benchmarks/issues
.. _Fork: https://help.github.com/articles/fork-a-repo
.. _synchronization-benchmarks: https://github.com/ARM-software/synchronization-benchmarks
.. _Git guidelines: http://git-scm.com/book/ch5-2.html
.. _automatically closed: https://help.github.com/articles/closing-issues-via-commit-messages
.. _license.rst: ./license.rst
.. _pull request: https://help.github.com/articles/using-pull-requests
