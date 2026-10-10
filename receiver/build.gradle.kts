plugins { kotlin("jvm") version "2.3.10" }
repositories { mavenCentral() }
kotlin { jvmToolchain(17); sourceSets.main { kotlin.srcDirs("src", "upstream") }; sourceSets.test { kotlin.srcDirs("tests") }; compilerOptions { freeCompilerArgs.add("-Xvalue-classes") } }
dependencies {
    implementation(files("libs/kotlinx-serialization-core-jvm-1.10.0.jar"))
    implementation(files("../third_party/monaka-protocol-v22/jvm/libs/monaka-protocol-jvm-0.1.0.jar"))
    implementation(files("../third_party/monaka-protocol-v22/jvm/libs/gson-2.11.0.jar"))
}
tasks.jar {
    archiveFileName.set("monaka-common-world-receiver.jar")
    isPreserveFileTimestamps = false; isReproducibleFileOrder = true
    manifest { attributes["Main-Class"] = "dev.monaka.tracking.ReceiverMainKt" }
}
tasks.processResources { from("build-identity.properties") }
tasks.register("runtimeDependencies") {
    doLast {
        layout.buildDirectory.file("runtime-classpath.txt").get().asFile.writeText(
            configurations.runtimeClasspath.get().files.sortedBy { it.name }.joinToString("\n") { it.absolutePath }+"\n")
    }
}
tasks.register<JavaExec>("receiverTest") {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.monaka.tracking.ReceiverTestsKt")
    jvmArgs("-Dfile.encoding=UTF-8")
    systemProperty("receiver.root", projectDir.parentFile.absolutePath)
}
