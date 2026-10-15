plugins { kotlin("jvm") version "2.3.10" }
repositories { mavenCentral() }
dependencies {
    implementation(files("../third_party/mvip/jvm/libs/monaka-input-protocol.jar"))
    implementation(files("../third_party/mvip/jvm/libs/gson-2.11.0.jar"))
}
kotlin { jvmToolchain(17) }
tasks.jar {
    archiveFileName.set("monaka-bridge-capabilities.jar")
    isPreserveFileTimestamps = false
    isReproducibleFileOrder = true
}
