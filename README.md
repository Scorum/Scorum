# Introducing Scorum

Scorum platform has three core functions:
 - Blogging platform where authors and readers will be rewarded for creating and engaging with content
 - Statistical centers where fans can browse and authors can use Microsoft’s Power BI tool to integrate data-rich visuals into their content
- Commission-free betting exchange where fans can place bets against each other using Scorum Coins (SCR)
Scorum’s blockchain protocol is built on the Graphene Framework and utilizes a delegated proof of stake consensus.

# Public Announcement & Discussion

The [Scorum team](https://scorumcoins.com/en-us/team) has been hard at work developing the blogging platform and the [statistics center](http://stats.scorum.com/).

Find out more as we take the project public through the following channels:
 - Get the latest updates and chat with us on [Telegram](https://telegram.me/SCORUM), [Facebook](https://www.facebook.com/SCORUM.COMMUNITY/), and [Twitter](https://twitter.com/SCORUM_en)
 - Read more about our vision on [Steemit](https://steemit.com/@scorum.community)
 - Join our [affiliate program](https://scorumcoins.com/en-us/affiliate) to get Scorum Coins for free or [apply for whitelist](https://scorumcoins.com/en-us/whitelist) to get coins with a discount

# No Support & No Warranty

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

# Blockchain consensus rules

Rather than attempt to describe the rules of the blockchain, it is up to
each individual to inspect the code to understand the consensus rules.

# Quickstart

Just want to get up and running quickly?  Try deploying a prebuilt
dockerized container.

## Dockerized Node

    mkdir /opt/scorumd
    # put your config file in /opt/scorumd/config.ini otherwise default config will be used.

    docker run \
        -v /opt/scorumd:/var/lib/scorumd \
        -d -p 2001:2001 -p 8090:8090 --name scorumd \
        scorum/release:0.0.1.680af90

    cd /opt/scorumd/logs
    tail -f current

# Seed Nodes

A list of some seed nodes to get you started can be found in
[doc/seednodes.txt](doc/seednodes.txt). This list is embedded into default config.ini.

# Building

See [doc/building.md](doc/building.md) for detailed build instructions, including
compile-time options, and specific commands for Linux (Ubuntu LTS) or macOS X.

# System Requirements

For a full node, you need at least 30GB of space available. Scorumd uses a memory mapped file which currently holds 2GB of data and by default is set to use up to 20GB. It's highly recommended to run scorumd on a fast disk such as an SSD or by placing the shared memory files in a ramdisk and using the `--shard-file-dir=/path` command line option to specify where. Any CPU with decent single core performance should be sufficient.

# Main net chain_id
genesis.json hash sum: `db4007d45f04c1403a7e66a5c66b5b1cdfc2dde8b5335d1d2f116d592ca3dbb1`
