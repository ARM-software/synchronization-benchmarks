import static groovy.io.FileType.FILES
import static groovy.io.FileType.DIRECTORIES

def getRepoURL() {
  sh "git config --get remote.origin.url > .git/remote-url"
  return readFile(".git/remote-url").trim()
}

void setBuildStatus(String message, String state, String context) {
  repoUrl = getRepoURL();
  step([
      $class: "GitHubCommitStatusSetter",
	  reposSource: [$class: "ManuallyEnteredRepositorySource", url: repoUrl],
      contextSource: [$class: "ManuallyEnteredCommitContextSource", context: context],
      errorHandlers: [[$class: "ChangingBuildStatusErrorHandler", result: "UNSTABLE"]],
      statusResultSource: [ $class: "ConditionalStatusResultSource", results: [[$class: "AnyBuildResult", message: message, state: state]] ],
      statusBackrefSource: [ $class: "ManuallyEnteredBackrefSource", backref: ""]
  ]);
}

node {
    stage('checkout') {
        checkout scm
    }

    stage('Build') {
        setBuildStatus("Building code", 'PENDING', 'Build');
        def fails = []
        def dir = new File("${env.WORKSPACE}/benchmarks/");
        dir.traverse(type: DIRECTORIES, maxDepth: 0) {
            //build
            try {
                sh "make -C ${it}"
            }
            catch (exc) {
                fails.add(it.toString().substring(env.WORKSPACE.length()))
            }
        }
        if (fails) {
            setBuildStatus("${fails} failed to build", 'FAILURE', 'Build');
            error "${fails} failed to build"
        }
        setBuildStatus("Build Successful!", 'SUCCESS', 'Build');
    }

    stage('Test') {
        setBuildStatus("Testing code", 'PENDING', 'Test');

        fails = []
        dir = new File("${env.WORKSPACE}/benchmarks/");
        // Run all scripts starting with the prefix "test"
        dir.traverse(type: DIRECTORIES, maxDepth: 0) {
            def scr = new File("${it}/scripts/")
            scr.traverse(type: FILES, filter: ~/.*\/test[^\/]*/) {
                try {
                    sh "${it}"
                }
                catch (exc) {
                    fails.add(it.toString().substring(env.WORKSPACE.length()))
                }
            }
        }
        if (fails) {
            setBuildStatus("Tests scripts: ${fails} Failed", 'FAILURE', 'Test');
            error "Tests scripts: ${fails} Failed"
        }
        setBuildStatus("Tests Passed!", 'SUCCESS', 'Test');
    }

}

