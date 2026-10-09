// Release CI: builds the DLL on the maintainer's Windows build machine and
// publishes GitHub releases.
//   push to main     -> stable release vX.Y.Z, marked Latest (X.Y from VERSION)
//   push to release  -> pre-release vX.Y.Z-betaN
// Nothing is built when only non-shipping files (installer/, docs) changed.
// Builds started by branch indexing never publish.
// The build tooling lives in a private companion repo; you don't need any of
// this to build the mod yourself -- see CvGameCoreDLL_Expansion2/build.bat.
pipeline {
    agent any
    options {
        disableConcurrentBuilds()
        timeout(time: 120, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '20', artifactNumToKeepStr: '5'))
    }
    stages {
        stage('Build and release') {
            steps {
                dir('.kek-build') {
                    git url: 'https://github.com/OBLASTWAR/kek-mod-build.git',
                        credentialsId: 'OBLASTWAR_GITHUB_LOGIN', branch: 'main'
                }
                script {
                    def publish = !currentBuild.getBuildCauses('jenkins.branch.BranchEventCause').isEmpty() ||
                                  !currentBuild.getBuildCauses('hudson.model.Cause$UserIdCause').isEmpty()
                    def kind = env.BRANCH_NAME == 'main' ? 'stable' : 'beta'
                    withCredentials([usernamePassword(credentialsId: 'OBLASTWAR_GITHUB_LOGIN',
                                         usernameVariable: 'GH_USER', passwordVariable: 'GITHUB_TOKEN'),
                                     file(credentialsId: 'kekmod-secrets-h', variable: 'KEK_SECRETS')]) {
                        sh "bash .kek-build/jenkins.sh ${kind} ${publish}"
                    }
                }
            }
        }
    }
    post {
        always { archiveArtifacts artifacts: 'out/**', allowEmptyArchive: true }
    }
}
