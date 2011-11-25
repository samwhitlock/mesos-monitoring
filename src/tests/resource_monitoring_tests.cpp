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

#include <gtest/gtest.h>

/*
 * TODO for testing lxc
 *
 * mock LxcResourceMonitor so that you can control the following methods
 * - getControlGroupDoubleValue (for memory and cpu separately)
 * - getContainerStartTime: just have it return some constant
 *  
 * What you need to verify with this:
 * -verify the usage report you get back after each call is what you expect (make your own)
 * -DO NOT read the internal state of LxcResourceMonitor! You should be free to change this in the future 
 *  without worrying about breaking tests so long as it outputs the same thing.
 */
