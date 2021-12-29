# Copy source code for mbedTLS into this directory
#
# You'll need to download mbedTLS from the official ARMmbed repository and
# place the files here. We recommend that you pick the latest version of 2.16
# LTS release in order to have up-to-date security fixes.

# Note that this SDK will not negotiate the TLS Maximum Fragment Length even if
# it is enabled in mbedTLS' configuration. Therefore, you should ensure content
# buffers used by mbedTLS are at least 16384 bytes in length to use the largest
# maximum record length supported by TLS.
