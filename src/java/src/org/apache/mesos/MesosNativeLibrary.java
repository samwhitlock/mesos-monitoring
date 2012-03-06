/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.apache.mesos;

public class MesosNativeLibrary {
  /**
   * Attempts to load the native library if it was not previously
   * loaded (e.g., by either {@link System.load} or {@link
   * System.loadLibrary}).
   */
  public static void load() {
    // Our JNI library will actually set 'loaded' to true once it is
    // loaded, that way the library can get loaded by a user via
    // 'System.load' in the event that they want to specify an
    // absolute path and we won't try and reload the library ourselves
    // (which would probably fail because 'java.library.path' might
    // not be set).
    if (!loaded) {
      final String MESOS_NATIVE_LIBRARY = System.getenv("MESOS_NATIVE_LIBRARY");
      if (MESOS_NATIVE_LIBRARY != null) {
        try {
          System.load(MESOS_NATIVE_LIBRARY);
        } catch (UnsatisfiedLinkError error) {
          System.err.println("Failed to load native Mesos library at " +
                             MESOS_NATIVE_LIBRARY);
          throw error;
        }
      } else {
        try {
          System.loadLibrary("mesos");
        } catch (UnsatisfiedLinkError error) {
          System.err.println("Failed to load native Mesos library from " +
                             System.getenv("java.library.path"));
          throw error;
        }
      }
    }
  }

  private static boolean loaded = false;
}
